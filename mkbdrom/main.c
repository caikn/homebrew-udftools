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

static void init_bdrom_disc(struct udf_disc *disc, uint32_t blocksize, uint32_t blocks, const char *label)
{
	int i;

	udf_init_disc(disc);

	disc->blocksize = blocksize;
	disc->blocks = blocks;
	disc->head->blocks = blocks;
	disc->udf_lvd[0]->logicalBlockSize = cpu_to_le32(blocksize);
	udf_set_version(disc, 0x0250);

	disc->flags |= FLAG_EFE | FLAG_METADATA | FLAG_BOOTAREA_ERASE;
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
	uint32_t next_offset;

	disc->metadata_start = 2;
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

	return next_offset;
}

static int write_func(struct udf_disc *disc, struct udf_extent *ext)
{
	static char *buffer = NULL;
	static size_t bufferlen = 0;
	int fd = *(int *)disc->write_data;
	ssize_t length;
	struct udf_desc *desc;
	struct udf_data *data;

	if (buffer == NULL)
	{
		bufferlen = disc->blocksize;
		buffer = calloc(bufferlen, 1);
		if (buffer == NULL)
			return -1;
	}

	if (ext->space_type == USPACE || ext->space_type == RESERVED)
		return 0;

	desc = ext->head;
	while (desc != NULL)
	{
		if (lseek(fd, (off_t)(ext->start + desc->offset) * disc->blocksize, SEEK_SET) < 0)
		{
			fprintf(stderr, "%s: Error: lseek failed: %s\n", appname, strerror(errno));
			return -1;
		}

		data = desc->data;
		while (data != NULL)
		{
			uint64_t remaining = data->length;
			uint8_t *src = (uint8_t *)data->buffer;

			while (remaining > 0)
			{
				size_t chunk = remaining > bufferlen ? bufferlen : remaining;
				memcpy(buffer, src, chunk);
				if (chunk < bufferlen)
					memset(buffer + chunk, 0, bufferlen - chunk);

				length = (chunk + disc->blocksize - 1) & ~(disc->blocksize - 1);
				if (write_nointr(fd, buffer, length) != length)
				{
					fprintf(stderr, "%s: Error: write failed: %s\n", appname, strerror(errno));
					return -1;
				}

				src += chunk;
				remaining -= chunk;
			}
			data = data->next;
		}

		desc = desc->next;
	}
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

static uint64_t scan_dir_size(const char *path)
{
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	char fullpath[4096];
	uint64_t total = 0;

	dir = opendir(path);
	if (!dir)
		return 0;

	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (entry->d_name[0] == '.')
			continue;
		snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
		if (stat(fullpath, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			total += scan_dir_size(fullpath);
		else if (S_ISREG(st.st_mode))
			total += st.st_size;
	}
	closedir(dir);
	return total;
}

int main(int argc, char *argv[])
{
	struct udf_disc plan_disc;
	struct udf_disc disc;
	struct udf_extent *plan_pspace;
	struct udf_extent *pspace;
	char *source_dir = NULL;
	char *output_file = NULL;
	char *label = "BLURAY";
	uint32_t disc_capacity = 0;
	uint32_t blocksize = 2048;
	uint32_t estimated_blocks;
	uint32_t required_blocks;
	uint64_t src_bytes;
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
		if (stat(source_dir, &st) != 0 || !S_ISDIR(st.st_mode))
		{
			fprintf(stderr, "%s: Error: Source '%s' is not a directory\n", appname, source_dir);
			exit(1);
		}
	}

	src_bytes = scan_dir_size(source_dir);
	estimated_blocks = (uint32_t)((src_bytes + blocksize - 1) / blocksize) + 2048;
	printf("Source size: %"PRIu64" bytes (%"PRIu32" estimated blocks)\n", src_bytes, estimated_blocks);

	init_bdrom_disc(&plan_disc, blocksize, disc_capacity ? disc_capacity : estimated_blocks, label);
	split_space(&plan_disc);
	plan_pspace = require_partition_space(&plan_disc);
	required_blocks = pack_source_tree(&plan_disc, plan_pspace, source_dir) + 512;
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

	split_space(&disc);
	setup_vrs(&disc);
	setup_lvid(&disc, next_extent(disc.head, LVID));
	setup_anchor(&disc);

	pspace = require_partition_space(&disc);
	printf("Packing files from: %s\n", source_dir);
	pack_source_tree(&disc, pspace, source_dir);
	disc.metadata_mirror_start = pspace->blocks - disc.metadata_blocks;
	setup_metadata(&disc, pspace);
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
