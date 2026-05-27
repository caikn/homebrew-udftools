#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "libudffs.h"
#include "file.h"
#include "defaults.h"
#include "pack.h"

extern const char *appname;

struct file_entry *file_list_head = NULL;
static struct file_entry *file_list_tail = NULL;
static struct udf_disc *pack_progress_disc = NULL;
static const char *pack_progress_label = NULL;
static uint64_t pack_progress_total = 0;
static uint64_t pack_progress_current = 0;

struct dir_item {
	char *name;
};

static int compare_dir_items(const void *left, const void *right)
{
	const struct dir_item *a = left;
	const struct dir_item *b = right;

	return strcmp(a->name, b->name);
}

static struct dir_item *read_sorted_directory(DIR *dir, size_t *count)
{
	struct dir_item *items = NULL;
	struct dirent *entry;
	size_t used = 0;
	size_t capacity = 0;

	*count = 0;

	while ((entry = readdir(dir)) != NULL)
	{
		struct dir_item *grown;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (entry->d_name[0] == '.')
			continue;

		if (used == capacity)
		{
			size_t next_capacity = capacity ? capacity * 2 : 16;

			grown = realloc(items, next_capacity * sizeof(*items));
			if (!grown)
			{
				fprintf(stderr, "%s: Error: realloc failed: %s\n", appname, strerror(errno));
				*count = SIZE_MAX;
				free(items);
				return NULL;
			}
			items = grown;
			capacity = next_capacity;
		}

		items[used].name = strdup(entry->d_name);
		if (!items[used].name)
		{
			size_t i;

			fprintf(stderr, "%s: Error: strdup failed: %s\n", appname, strerror(errno));
			*count = SIZE_MAX;
			for (i = 0; i < used; i++)
				free(items[i].name);
			free(items);
			return NULL;
		}
		used++;
	}

	qsort(items, used, sizeof(*items), compare_dir_items);
	*count = used;
	return items;
}

static void free_dir_items(struct dir_item *items, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		free(items[i].name);
	free(items);
}

static uint32_t next_metadata_offset(struct udf_extent *pspace, uint32_t blocksize)
{
	if (!pspace->tail)
		return 0;

	return pspace->tail->offset + (uint32_t)((pspace->tail->length + blocksize - 1) / blocksize);
}

static uint64_t total_file_bytes(void)
{
	struct file_entry *entry = file_list_head;
	uint64_t total = 0;

	while (entry)
	{
		total += entry->size;
		entry = entry->next;
	}

	return total;
}

static void print_progress(uint64_t written, uint64_t total)
{
	int percent;
	int filled;
	int i;

	if (total == 0)
		percent = 100;
	else
		percent = (int)((written * 100) / total);

	filled = percent / 5;
	if (filled > 20)
		filled = 20;

	fprintf(stderr, "\rWriting file data: [");
	for (i = 0; i < 20; i++)
		fputc(i < filled ? '#' : '.', stderr);
	fprintf(stderr, "] %3d%% (%"PRIu64"/%"PRIu64" bytes)", percent, written, total);
	fflush(stderr);
}

void set_pack_progress(struct udf_disc *disc, const char *label, uint64_t total)
{
	pack_progress_disc = disc;
	pack_progress_label = label;
	pack_progress_total = total;
	pack_progress_current = 0;

	if (pack_progress_disc && pack_progress_disc->progress && pack_progress_total == 0)
		pack_progress_disc->progress(pack_progress_disc, pack_progress_label, 0, 0);
}

static void advance_pack_progress(void)
{
	if (!pack_progress_disc || !pack_progress_disc->progress || !pack_progress_label || pack_progress_total == 0)
		return;

	pack_progress_current++;
	pack_progress_disc->progress(pack_progress_disc, pack_progress_label, pack_progress_current, pack_progress_total);
}

void reset_file_entries(void)
{
	struct file_entry *entry = file_list_head;

	while (entry)
	{
		struct file_entry *next = entry->next;

		free(entry->source_path);
		free(entry);
		entry = next;
	}

	file_list_head = NULL;
	file_list_tail = NULL;
}

static void add_file_entry(const char *path, struct udf_desc *desc, const struct stat *st)
{
	struct file_entry *fe = malloc(sizeof(struct file_entry));
	if (!fe)
	{
		fprintf(stderr, "%s: Error: malloc failed: %s\n", appname, strerror(errno));
		exit(1);
	}
	fe->source_path = strdup(path);
	fe->desc = desc;
	fe->data_start = 0;
	fe->size = st->st_size;
	fe->device = st->st_dev;
	fe->inode = st->st_ino;
	fe->next = NULL;

	if (file_list_tail)
		file_list_tail->next = fe;
	else
		file_list_head = fe;
	file_list_tail = fe;
}

static int pack_file(struct udf_disc *disc, struct udf_extent *pspace, const char *filepath, struct udf_desc *parent_desc, uint32_t *next_offset)
{
	struct stat st;
	struct udf_desc *file_desc;
	struct extendedFileEntry *efe;
	const char *basename;
	dstring encoded_name[256];
	size_t name_len;

	if (lstat(filepath, &st) != 0)
	{
		fprintf(stderr, "%s: Error: Cannot stat '%s': %s\n", appname, filepath, strerror(errno));
		return -1;
	}

	if (S_ISLNK(st.st_mode))
	{
		fprintf(stderr, "%s: Warning: Skipping symlink '%s'\n", appname, filepath);
		return 0;
	}

	if (!S_ISREG(st.st_mode))
		return 0;

	basename = strrchr(filepath, '/');
	basename = basename ? basename + 1 : filepath;

	if (basename[0] == '.')
		return 0;

	encoded_name[0] = 8;
	name_len = strlen(basename);
	if (name_len > 254)
		name_len = 254;
	memcpy(encoded_name + 1, basename, name_len);
	name_len += 1;

	file_desc = udf_create(disc, pspace, encoded_name, name_len, *next_offset, parent_desc, 0, ICBTAG_FILE_TYPE_REGULAR, 0);
	if (!file_desc)
	{
		fprintf(stderr, "%s: Error: Cannot create file entry for '%s'\n", appname, basename);
		return -1;
	}
	*next_offset = next_metadata_offset(pspace, disc->blocksize);

	efe = (struct extendedFileEntry *)file_desc->data->buffer;

	if (st.st_size == 0)
	{
		efe->informationLength = cpu_to_le64(0);
		efe->objectSize = cpu_to_le64(0);
		efe->logicalBlocksRecorded = cpu_to_le64(0);
		efe->descTag = query_tag(disc, pspace, file_desc, 1);
		return 0;
	}

	efe->icbTag.flags = cpu_to_le16(ICBTAG_FLAG_AD_LONG);
	efe->informationLength = cpu_to_le64(st.st_size);
	efe->objectSize = cpu_to_le64(st.st_size);
	efe->logicalBlocksRecorded = cpu_to_le64((st.st_size + disc->blocksize - 1) / disc->blocksize);
	efe->lengthAllocDescs = cpu_to_le32(sizeof(long_ad));

	size_t new_len = sizeof(struct extendedFileEntry) + sizeof(long_ad);
	void *new_buf = realloc(file_desc->data->buffer, new_len);
	if (!new_buf)
	{
		fprintf(stderr, "%s: Error: realloc failed: %s\n", appname, strerror(errno));
		return -1;
	}
	file_desc->data->buffer = new_buf;
	file_desc->data->length = new_len;
	file_desc->length = new_len;

	efe = (struct extendedFileEntry *)file_desc->data->buffer;
	{
		long_ad *lad = (long_ad *)(efe->extendedAttrAndAllocDescs);
		lad->extLength = cpu_to_le32((uint32_t)st.st_size);
		lad->extLocation.logicalBlockNum = cpu_to_le32(0);
		lad->extLocation.partitionReferenceNum = cpu_to_le16(0);
		memset(lad->impUse, 0, sizeof(lad->impUse));
	}

	efe->descTag = query_tag(disc, pspace, file_desc, 1);

	add_file_entry(filepath, file_desc, &st);
	advance_pack_progress();

	return 0;
}

int pack_directory(struct udf_disc *disc, struct udf_extent *pspace, const char *source_path, struct udf_desc *parent_desc, uint32_t *next_offset)
{
	DIR *dir;
	struct dir_item *items;
	char path[4096];
	struct stat st;
	struct udf_desc *dir_desc;
	size_t count;
	size_t index;

	dir = opendir(source_path);
	if (!dir)
	{
		fprintf(stderr, "%s: Error: Cannot open directory '%s': %s\n", appname, source_path, strerror(errno));
		return -1;
	}

	items = read_sorted_directory(dir, &count);
	closedir(dir);
	if (!items && count == SIZE_MAX)
		return -1;

	for (index = 0; index < count; index++)
	{
		if (snprintf(path, sizeof(path), "%s/%s", source_path, items[index].name) >= (int)sizeof(path))
		{
			fprintf(stderr, "%s: Warning: Path too long, skipping '%s/%s'\n", appname, source_path, items[index].name);
			continue;
		}

		if (lstat(path, &st) != 0)
		{
			fprintf(stderr, "%s: Warning: Cannot stat '%s': %s, skipping\n", appname, path, strerror(errno));
			continue;
		}

		if (S_ISLNK(st.st_mode))
		{
			fprintf(stderr, "%s: Warning: Skipping symlink '%s'\n", appname, path);
			continue;
		}

		if (S_ISDIR(st.st_mode))
		{
			dstring encoded_name[256];
			size_t name_len = strlen(items[index].name);
			if (name_len > 254)
				name_len = 254;
			encoded_name[0] = 8;
			memcpy(encoded_name + 1, items[index].name, name_len);
			name_len += 1;

			dir_desc = udf_mkdir(disc, pspace, encoded_name, name_len, *next_offset, parent_desc);
			if (!dir_desc)
			{
				fprintf(stderr, "%s: Error: Cannot create directory '%s'\n", appname, items[index].name);
				free_dir_items(items, count);
				return -1;
			}
			*next_offset = next_metadata_offset(pspace, disc->blocksize);
			advance_pack_progress();

			if (pack_directory(disc, pspace, path, dir_desc, next_offset) < 0)
			{
				free_dir_items(items, count);
				return -1;
			}

			struct extendedFileEntry *efe = (struct extendedFileEntry *)dir_desc->data->buffer;
			efe->descTag = query_tag(disc, pspace, dir_desc, 1);
		}
		else if (S_ISREG(st.st_mode))
		{
			if (pack_file(disc, pspace, path, parent_desc, next_offset) < 0)
			{
				free_dir_items(items, count);
				return -1;
			}
		}
	}

	free_dir_items(items, count);
	return 0;
}

uint32_t layout_file_data(struct udf_disc *disc, struct udf_extent *pspace, uint32_t start_offset)
{
	struct file_entry *entry = file_list_head;
	uint32_t next_offset = start_offset;

	while (entry)
	{
		struct extendedFileEntry *efe;
		long_ad *lad;
		uint32_t data_blocks;

		if (entry->size == 0)
		{
			entry = entry->next;
			continue;
		}

		data_blocks = (uint32_t)((entry->size + disc->blocksize - 1) / disc->blocksize);
		entry->data_start = next_offset;
		efe = (struct extendedFileEntry *)entry->desc->data->buffer;
		lad = (long_ad *)(efe->extendedAttrAndAllocDescs);
		lad->extLocation.logicalBlockNum = cpu_to_le32(entry->data_start);
		lad->extLocation.partitionReferenceNum = cpu_to_le16(0);
		efe->descTag = query_tag(disc, pspace, entry->desc, 1);
		next_offset += data_blocks;
		entry = entry->next;
	}

	return next_offset;
}

int write_file_data(int fd, struct udf_disc *disc, struct udf_extent *pspace)
{
	struct file_entry *fe = file_list_head;
	char buf[65536];
	uint32_t pspace_start = pspace->start;
	uint64_t total_bytes = total_file_bytes();
	uint64_t written_bytes = 0;
	int show_progress = isatty(STDERR_FILENO);
	int last_percent = -1;

	if (show_progress)
		print_progress(0, total_bytes);

	while (fe)
	{
		off_t offset = (off_t)(pspace_start + fe->data_start) * disc->blocksize;
		int src_fd = open(fe->source_path, O_RDONLY | O_NOFOLLOW);
		struct stat current;
		if (src_fd < 0)
		{
			fprintf(stderr, "%s: Error: Cannot open '%s': %s\n", appname, fe->source_path, strerror(errno));
			return -1;
		}

		if (fstat(src_fd, &current) != 0)
		{
			fprintf(stderr, "%s: Error: Cannot stat '%s': %s\n", appname, fe->source_path, strerror(errno));
			close(src_fd);
			return -1;
		}

		if (!S_ISREG(current.st_mode) || current.st_dev != fe->device || current.st_ino != fe->inode || (uint64_t)current.st_size != fe->size)
		{
			fprintf(stderr, "%s: Error: Source file '%s' changed during image creation\n", appname, fe->source_path);
			close(src_fd);
			return -1;
		}

		if (lseek(fd, offset, SEEK_SET) < 0)
		{
			fprintf(stderr, "%s: Error: lseek failed: %s\n", appname, strerror(errno));
			close(src_fd);
			return -1;
		}

		uint64_t remaining = fe->size;
		while (remaining > 0)
		{
			size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
			ssize_t n = read_nointr(src_fd, buf, chunk);
			int percent;
			if (n <= 0)
			{
				fprintf(stderr, "%s: Error: read '%s' failed: %s\n", appname, fe->source_path, strerror(errno));
				close(src_fd);
				return -1;
			}
			if (write_nointr(fd, buf, n) != n)
			{
				fprintf(stderr, "%s: Error: write failed: %s\n", appname, strerror(errno));
				close(src_fd);
				return -1;
			}
			written_bytes += n;
			if (show_progress)
			{
				if (total_bytes == 0)
					percent = 100;
				else
					percent = (int)((written_bytes * 100) / total_bytes);
				if (percent != last_percent)
				{
					print_progress(written_bytes, total_bytes);
					last_percent = percent;
				}
			}
			remaining -= n;
		}
		close(src_fd);
		fe = fe->next;
	}

	if (show_progress)
		fprintf(stderr, "\n");

	return 0;
}
