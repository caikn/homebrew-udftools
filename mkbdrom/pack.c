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

static void add_file_entry(const char *path, uint32_t data_start, uint64_t size)
{
	struct file_entry *fe = malloc(sizeof(struct file_entry));
	if (!fe)
	{
		fprintf(stderr, "%s: Error: malloc failed: %s\n", appname, strerror(errno));
		exit(1);
	}
	fe->source_path = strdup(path);
	fe->data_start = data_start;
	fe->size = size;
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
	short_ad *sad;
	uint32_t data_blocks, data_start;
	const char *basename;
	dstring encoded_name[256];
	size_t name_len;

	if (stat(filepath, &st) != 0)
	{
		fprintf(stderr, "%s: Error: Cannot stat '%s': %s\n", appname, filepath, strerror(errno));
		return -1;
	}

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
	*next_offset = file_desc->offset + 1;

	efe = (struct extendedFileEntry *)file_desc->data->buffer;

	if (st.st_size == 0)
	{
		efe->informationLength = cpu_to_le64(0);
		efe->objectSize = cpu_to_le64(0);
		efe->logicalBlocksRecorded = cpu_to_le64(0);
		efe->descTag = query_tag(disc, pspace, file_desc, 1);
		return 0;
	}

	data_blocks = (st.st_size + disc->blocksize - 1) / disc->blocksize;
	data_start = *next_offset;
	*next_offset = data_start + data_blocks;

	efe->icbTag.flags = cpu_to_le16(ICBTAG_FLAG_AD_SHORT);
	efe->informationLength = cpu_to_le64(st.st_size);
	efe->objectSize = cpu_to_le64(st.st_size);
	efe->logicalBlocksRecorded = cpu_to_le64(data_blocks);
	efe->lengthAllocDescs = cpu_to_le32(sizeof(short_ad));

	size_t new_len = sizeof(struct extendedFileEntry) + sizeof(short_ad);
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
	sad = (short_ad *)(efe->extendedAttrAndAllocDescs);
	sad->extLength = cpu_to_le32((uint32_t)st.st_size);
	sad->extPosition = cpu_to_le32(data_start);

	efe->descTag = query_tag(disc, pspace, file_desc, 1);

	add_file_entry(filepath, data_start, st.st_size);

	return 0;
}

int pack_directory(struct udf_disc *disc, struct udf_extent *pspace, const char *source_path, struct udf_desc *parent_desc, uint32_t *next_offset)
{
	DIR *dir;
	struct dirent *entry;
	char path[4096];
	struct stat st;
	struct udf_desc *dir_desc;

	dir = opendir(source_path);
	if (!dir)
	{
		fprintf(stderr, "%s: Error: Cannot open directory '%s': %s\n", appname, source_path, strerror(errno));
		return -1;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (entry->d_name[0] == '.')
			continue;

		snprintf(path, sizeof(path), "%s/%s", source_path, entry->d_name);

		if (stat(path, &st) != 0)
		{
			fprintf(stderr, "%s: Warning: Cannot stat '%s': %s, skipping\n", appname, path, strerror(errno));
			continue;
		}

		if (S_ISDIR(st.st_mode))
		{
			dstring encoded_name[256];
			size_t name_len = strlen(entry->d_name);
			if (name_len > 254)
				name_len = 254;
			encoded_name[0] = 8;
			memcpy(encoded_name + 1, entry->d_name, name_len);
			name_len += 1;

			dir_desc = udf_mkdir(disc, pspace, encoded_name, name_len, *next_offset, parent_desc);
			if (!dir_desc)
			{
				fprintf(stderr, "%s: Error: Cannot create directory '%s'\n", appname, entry->d_name);
				closedir(dir);
				return -1;
			}
			*next_offset = dir_desc->offset + 1;

			if (pack_directory(disc, pspace, path, dir_desc, next_offset) < 0)
			{
				closedir(dir);
				return -1;
			}

			struct extendedFileEntry *efe = (struct extendedFileEntry *)dir_desc->data->buffer;
			efe->descTag = query_tag(disc, pspace, dir_desc, 1);
		}
		else if (S_ISREG(st.st_mode))
		{
			if (pack_file(disc, pspace, path, parent_desc, next_offset) < 0)
			{
				closedir(dir);
				return -1;
			}
		}
	}

	closedir(dir);
	return 0;
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
		int src_fd = open(fe->source_path, O_RDONLY);
		if (src_fd < 0)
		{
			fprintf(stderr, "%s: Error: Cannot open '%s': %s\n", appname, fe->source_path, strerror(errno));
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
