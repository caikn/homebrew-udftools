#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <sys/disk.h>
#endif

#include "libudffs.h"
#include "mkudffs.h"
#include "file.h"
#include "defaults.h"
#include "pack.h"

const char *appname;

struct progress_state
{
	int enabled;
	int last_percent;
	char last_label[64];
};

struct source_stats
{
	uint64_t bytes;
	uint64_t entries;
};

static void print_phase_progress(struct udf_disc *disc, const char *label, uint64_t current, uint64_t total)
{
	struct progress_state *state = disc->progress_data;
	uint64_t display_current = current;
	int percent;
	int filled;
	int i;

	if (!state || !state->enabled)
		return;

	if (total && display_current > total)
		display_current = total;

	if (total == 0)
		percent = 100;
	else
		percent = (int)((display_current * 100) / total);

	if (percent == state->last_percent && strcmp(state->last_label, label) == 0)
		return;

	filled = percent / 5;
	if (filled > 20)
		filled = 20;

	fprintf(stderr, "\r%s: [", label);
	for (i = 0; i < 20; i++)
		fputc(i < filled ? '#' : '.', stderr);
	fprintf(stderr, "] %3d%% (%"PRIu64"/%"PRIu64")", percent, display_current, total);
	fflush(stderr);

	state->last_percent = percent;
	strncpy(state->last_label, label, sizeof(state->last_label) - 1);
	state->last_label[sizeof(state->last_label) - 1] = '\0';

	if ((total == 0) || (display_current >= total))
	{
		fputc('\n', stderr);
		state->last_percent = -1;
		state->last_label[0] = '\0';
	}
}

static void init_bdrom_disc(struct udf_disc *disc, uint32_t blocksize, uint32_t blocks, const char *label)
{
	int i;

	udf_init_disc(disc);

	disc->blocksize = blocksize;
	disc->blocks = blocks;
	disc->head->blocks = blocks;
	disc->udf_lvd[0]->logicalBlockSize = cpu_to_le32(blocksize);
	udf_set_version(disc, 0x0250);

	disc->flags |= FLAG_EFE | FLAG_METADATA | FLAG_BOOTAREA_ERASE | FLAG_BDROM;
	disc->flags &= ~FLAG_SPACE;

	for (i = 0; i < UDF_ALLOC_TYPE_SIZE; i++)
		disc->sizing[i] = default_sizing[default_media[MEDIA_TYPE_HD]][i];

	encode_string(disc, disc->udf_lvd[0]->logicalVolIdent, label, 128);
	encode_string(disc, disc->udf_pvd[0]->volIdent, label, 32);

	add_type1_partition(disc, 0);
	add_type2_metadata_partition(disc, 0);
}

static struct udf_extent *require_partition_space(struct udf_disc *disc)
{
	struct udf_extent *pspace = next_extent(disc->head, PSPACE);

	if (!pspace)
	{
		fprintf(stderr, "%s: Error: No partition space available\n", appname);
		exit(1);
	}

	return pspace;
}

static struct udf_desc *find_root_desc(struct udf_extent *pspace)
{
	struct udf_desc *desc = pspace->head;

	while (desc)
	{
		if (desc->ident == TAG_IDENT_EFE || desc->ident == TAG_IDENT_FE)
		{
			struct extendedFileEntry *efe = (struct extendedFileEntry *)desc->data->buffer;

			if (efe->icbTag.fileType == ICBTAG_FILE_TYPE_DIRECTORY)
				return desc;
		}
		desc = desc->next;
	}

	return NULL;
}

static uint32_t compute_metadata_blocks(struct udf_disc *disc, struct udf_extent *pspace)
{
	uint32_t last_offset = 0;
	struct udf_desc *desc = pspace->head;

	while (desc)
	{
		uint32_t end = desc->offset + (desc->length + disc->blocksize - 1) / disc->blocksize;

		if (end > last_offset)
			last_offset = end;
		desc = desc->next;
	}

	return last_offset - disc->metadata_start;
}

static uint32_t pack_source_tree(struct udf_disc *disc, struct udf_extent *pspace, const char *source_dir)
{
	struct udf_desc *root_desc;
	struct extendedFileEntry *root_efe;
	uint32_t data_start;
	uint32_t next_offset;
	uint32_t aligned_meta_blocks;
	struct udf_desc *d;

	disc->metadata_start = 32;
	setup_fileset(disc, pspace);
	setup_root(disc, pspace);

	root_desc = find_root_desc(pspace);
	if (!root_desc)
	{
		fprintf(stderr, "%s: Error: Cannot find root directory\n", appname);
		exit(1);
	}

	next_offset = root_desc->offset + 1;
	if (pack_directory(disc, pspace, source_dir, root_desc, &next_offset) < 0)
		exit(1);

	root_efe = (struct extendedFileEntry *)root_desc->data->buffer;
	root_efe->descTag = query_tag(disc, pspace, root_desc, 1);
	disc->metadata_blocks = compute_metadata_blocks(disc, pspace);

	aligned_meta_blocks = ((disc->metadata_blocks + 31) / 32) * 32;
	disc->metadata_blocks = aligned_meta_blocks;
	data_start = disc->metadata_start + disc->metadata_blocks;

	/* Recompute tags and fix directory short_ad positions to metadata-relative */
	for (d = pspace->head; d; d = d->next)
	{
		if (d->offset >= disc->metadata_start && d->offset < disc->metadata_start + disc->metadata_blocks)
		{
			struct extendedFileEntry *efe = (struct extendedFileEntry *)d->data->buffer;
			uint16_t dtag = le16_to_cpu(efe->descTag.tagIdent);

			if (dtag == TAG_IDENT_EFE && efe->icbTag.fileType == ICBTAG_FILE_TYPE_DIRECTORY)
			{
				uint16_t flags = le16_to_cpu(efe->icbTag.flags);
				if ((flags & ICBTAG_FLAG_AD_MASK) == ICBTAG_FLAG_AD_SHORT && le32_to_cpu(efe->lengthAllocDescs) >= sizeof(short_ad))
				{
					short_ad *sad = (short_ad *)&efe->extendedAttrAndAllocDescs[le32_to_cpu(efe->lengthExtendedAttr)];
					uint32_t pos = le32_to_cpu(sad->extPosition);
					if (pos >= disc->metadata_start)
						sad->extPosition = cpu_to_le32(pos - disc->metadata_start);
				}
			}
			*(tag *)d->data->buffer = query_tag(disc, pspace, d, 1);
		}
	}

	return layout_file_data(disc, pspace, data_start);
}

static int write_func(struct udf_disc *disc, struct udf_extent *ext)
{
	int fd = *(int *)disc->write_data;
	struct udf_desc *desc;
	struct udf_data *data;
	char *block_buf;
	uint32_t block_pos;

	block_buf = calloc(1, disc->blocksize);
	if (!block_buf)
		return -1;

	if (ext->space_type == USPACE || ext->space_type == RESERVED)
	{
		free(block_buf);
		return 0;
	}

	desc = ext->head;
	while (desc != NULL)
	{
		off_t base_offset = (off_t)(ext->start + desc->offset) * disc->blocksize;

		if (lseek(fd, base_offset, SEEK_SET) < 0)
		{
			fprintf(stderr, "%s: Error: lseek failed: %s\n", appname, strerror(errno));
			free(block_buf);
			return -1;
		}

		block_pos = 0;
		memset(block_buf, 0, disc->blocksize);

		data = desc->data;
		while (data != NULL)
		{
			uint8_t *src = (uint8_t *)data->buffer;
			uint64_t remaining = data->length;

			while (remaining > 0)
			{
				size_t space = disc->blocksize - block_pos;
				size_t copy = remaining < space ? remaining : space;

				memcpy(block_buf + block_pos, src, copy);
				block_pos += copy;
				src += copy;
				remaining -= copy;

				if (block_pos == disc->blocksize)
				{
					if (write_nointr(fd, block_buf, disc->blocksize) != (ssize_t)disc->blocksize)
					{
						fprintf(stderr, "%s: Error: write failed: %s\n", appname, strerror(errno));
						free(block_buf);
						return -1;
					}
					block_pos = 0;
					memset(block_buf, 0, disc->blocksize);
				}
			}
			data = data->next;
		}

		if (block_pos > 0)
		{
			if (write_nointr(fd, block_buf, disc->blocksize) != (ssize_t)disc->blocksize)
			{
				fprintf(stderr, "%s: Error: write failed: %s\n", appname, strerror(errno));
				free(block_buf);
				return -1;
			}
		}

		desc = desc->next;
	}

	free(block_buf);
	return 0;
}

static void usage(void)
{
	fprintf(stderr, "mkbdrom from udftools 2.3-macos\n"
		"Usage:\n"
		"\tmkbdrom --source <directory> [--label <label>] [--disc-capacity <blocks>] <output.iso>\n"
		"\n"
		"Creates a UDF 2.50 BD-ROM image with Metadata Partition from source directory.\n"
		"\n"
		"Options:\n"
		"\t--source <dir>          Source directory containing BDMV/CERTIFICATE structure\n"
		"\t--label <label>         Volume label (default: BLURAY)\n"
		"\t--disc-capacity <n>     Target disc capacity in blocks (default: auto from content)\n"
		"\t--blocksize <n>         Block size in bytes (default: 2048)\n"
	);
	exit(1);
}

static void scan_source_stats(const char *path, struct source_stats *stats)
{
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	char fullpath[4096];

	dir = opendir(path);
	if (!dir)
		return;

	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (entry->d_name[0] == '.')
			continue;
		snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
		if (lstat(fullpath, &st) != 0)
			continue;
		if (S_ISLNK(st.st_mode))
			continue;
		if (S_ISDIR(st.st_mode))
		{
			stats->entries++;
			scan_source_stats(fullpath, stats);
		}
		else if (S_ISREG(st.st_mode))
		{
			stats->bytes += st.st_size;
			stats->entries++;
		}
	}
	closedir(dir);
}

int main(int argc, char *argv[])
{
	struct udf_disc plan_disc;
	struct udf_disc disc;
	struct progress_state progress = { 0, -1, { 0 } };
	struct udf_extent *plan_pspace;
	struct udf_extent *pspace;
	char *source_dir = NULL;
	char *output_file = NULL;
	char *label = "BLURAY";
	uint32_t disc_capacity = 0;
	uint32_t blocksize = 2048;
	uint32_t estimated_blocks;
	uint32_t required_blocks;
	struct source_stats source_stats = { 0, 0 };
	int fd;
	int i;

	appname = "mkbdrom";

	if (!setlocale(LC_CTYPE, ""))
		fprintf(stderr, "%s: Error: Cannot set locale/codeset, fallback to default 7bit C ASCII\n", appname);

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--source") == 0 && i + 1 < argc)
			source_dir = argv[++i];
		else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc)
			label = argv[++i];
		else if (strcmp(argv[i], "--disc-capacity") == 0 && i + 1 < argc)
			disc_capacity = strtoul(argv[++i], NULL, 0);
		else if (strcmp(argv[i], "--blocksize") == 0 && i + 1 < argc)
			blocksize = strtoul(argv[++i], NULL, 0);
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			usage();
		else if (argv[i][0] != '-' && !output_file)
			output_file = argv[i];
		else
			usage();
	}

	if (!source_dir || !output_file)
		usage();

	{
		struct stat st;
		if (lstat(source_dir, &st) != 0 || !S_ISDIR(st.st_mode))
		{
			fprintf(stderr, "%s: Error: Source '%s' is not a directory\n", appname, source_dir);
			exit(1);
		}
	}

	scan_source_stats(source_dir, &source_stats);
	estimated_blocks = (uint32_t)((source_stats.bytes + blocksize - 1) / blocksize) + 2048;
	printf("Source size: %"PRIu64" bytes (%"PRIu32" estimated blocks)\n", source_stats.bytes, estimated_blocks);
	printf("Source entries: %"PRIu64"\n", source_stats.entries);

	init_bdrom_disc(&plan_disc, blocksize, disc_capacity ? disc_capacity : estimated_blocks, label);
	plan_disc.progress = print_phase_progress;
	plan_disc.progress_data = &progress;
	progress.enabled = isatty(STDERR_FILENO);
	split_space(&plan_disc);
	plan_pspace = require_partition_space(&plan_disc);
	set_pack_progress(&plan_disc, "Planning layout", source_stats.entries);
	required_blocks = pack_source_tree(&plan_disc, plan_pspace, source_dir);
	set_pack_progress(NULL, NULL, 0);

	/* Convert pspace-relative end to total disc blocks:
	 * add mirror region + non-pspace overhead (VRS, MVDS, RVDS, anchors, LVID) */
	{
		uint32_t non_pspace_overhead = plan_pspace->start + (plan_disc.blocks - plan_pspace->start - plan_pspace->blocks);
		required_blocks += plan_disc.metadata_blocks + 1 + non_pspace_overhead + 32;
	}
	reset_file_entries();

	if (disc_capacity)
	{
		if (disc_capacity < required_blocks)
		{
			fprintf(stderr, "%s: Error: --disc-capacity (%"PRIu32") is smaller than needed content blocks (%"PRIu32")\n", appname, disc_capacity, required_blocks);
			exit(1);
		}
		required_blocks = disc_capacity;
	}

	init_bdrom_disc(&disc, blocksize, required_blocks, label);

	fd = open(output_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		fprintf(stderr, "%s: Error: Cannot create '%s': %s\n", appname, output_file, strerror(errno));
		exit(1);
	}

	if (ftruncate(fd, (off_t)disc.blocks * disc.blocksize) != 0)
	{
		fprintf(stderr, "%s: Error: Cannot set file size: %s\n", appname, strerror(errno));
		close(fd);
		exit(1);
	}

	disc.write = write_func;
	disc.write_data = &fd;
	progress.enabled = isatty(STDERR_FILENO);
	disc.progress = print_phase_progress;
	disc.progress_data = &progress;

	split_space(&disc);
	setup_vrs(&disc);
	setup_lvid(&disc, next_extent(disc.head, LVID));
	setup_anchor(&disc);

	pspace = require_partition_space(&disc);
	printf("Packing files from: %s\n", source_dir);
	set_pack_progress(&disc, "Packing entries", source_stats.entries);
	{
		uint32_t file_data_end;
		uint32_t mirror_efe_offset;

		file_data_end = pack_source_tree(&disc, pspace, source_dir);

		/* Place mirror EFE and content at end of partition, after file data */
		mirror_efe_offset = pspace->blocks - disc.metadata_blocks - 1;
		if (file_data_end > mirror_efe_offset)
		{
			fprintf(stderr, "%s: Error: Source content changed during image creation; rerun mkbdrom\n", appname);
			close(fd);
			exit(1);
		}
		disc.metadata_mirror_start = mirror_efe_offset + 1;
	}
	set_pack_progress(NULL, NULL, 0);
	setup_metadata(&disc, pspace);
	disc.progress = NULL;
	disc.progress_data = NULL;

	/* Fix LVID: partition 0 free=0, partition 1 free=0/size=metadata_blocks */
	{
		uint32_t *freeSpace = (uint32_t *)&disc.udf_lvid->data[0];
		uint32_t *sizeTable = (uint32_t *)&disc.udf_lvid->data[sizeof(uint32_t) * le32_to_cpu(disc.udf_lvid->numOfPartitions)];
		freeSpace[0] = cpu_to_le32(0);
		freeSpace[1] = cpu_to_le32(0);
		sizeTable[0] = cpu_to_le32(pspace->blocks);
		sizeTable[1] = cpu_to_le32(disc.metadata_blocks);
	}

	/* Set partition access type to read-only */
	disc.udf_pd[0]->accessType = cpu_to_le32(1);

	/* Set write protection flags in FSD domain identifier suffix and recompute FSD tag */
	{
		struct domainIdentSuffix *dis = (struct domainIdentSuffix *)disc.udf_fsd->domainIdent.identSuffix;
		struct udf_desc *fsd_desc;
		dis->domainFlags = DOMAIN_FLAGS_HARD_WRITE_PROTECT | DOMAIN_FLAGS_SOFT_WRITE_PROTECT;
		fsd_desc = next_desc(pspace->head, TAG_IDENT_FSD);
		if (fsd_desc)
			disc.udf_fsd->descTag = query_tag(&disc, pspace, fsd_desc, 1);
	}

	setup_vds(&disc);

	printf("Writing image: %s\n", output_file);
	printf("  blocks=%"PRIu32", blocksize=%"PRIu32"\n", disc.blocks, disc.blocksize);

	if (write_disc(&disc) < 0)
	{
		fprintf(stderr, "%s: Error: Write failed\n", appname);
		close(fd);
		exit(1);
	}

	printf("Writing file data...\n");
	if (write_file_data(fd, &disc, pspace) < 0)
	{
		fprintf(stderr, "%s: Error: File data write failed\n", appname);
		close(fd);
		exit(1);
	}

	close(fd);
	printf("Done.\n");
	return 0;
}
