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

static int write_func(struct udf_disc *disc, struct udf_extent *ext)
{
	static char *buffer = NULL;
	static size_t bufferlen = 0;
	int fd = *(int *)disc->write_data;
	ssize_t length, offset;
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
		offset = 0;
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
	struct udf_disc disc;
	struct udf_extent *pspace;
	char *source_dir = NULL;
	char *output_file = NULL;
	char *label = "BLURAY";
	uint32_t disc_capacity = 0;
	uint32_t blocksize = 2048;
	uint32_t content_blocks;
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

	udf_init_disc(&disc);

	disc.blocksize = blocksize;
	disc.udf_lvd[0]->logicalBlockSize = cpu_to_le32(blocksize);
	udf_set_version(&disc, 0x0250);

	disc.flags |= FLAG_EFE | FLAG_METADATA | FLAG_BOOTAREA_ERASE;
	disc.flags &= ~FLAG_SPACE;

	for (i = 0; i < UDF_ALLOC_TYPE_SIZE; i++)
		disc.sizing[i] = default_sizing[default_media[MEDIA_TYPE_HD]][i];

	encode_string(&disc, disc.udf_lvd[0]->logicalVolIdent, label, 128);
	encode_string(&disc, disc.udf_pvd[0]->volIdent, label, 32);

	add_type1_partition(&disc, 0);
	add_type2_metadata_partition(&disc, 0);

	{
		uint64_t src_bytes = scan_dir_size(source_dir);
		content_blocks = (src_bytes / blocksize) + 2048;
		printf("Source size: %llu bytes (%"PRIu32" blocks + overhead)\n", src_bytes, content_blocks);
	}

	if (disc_capacity && disc_capacity > content_blocks)
		disc.blocks = disc_capacity;
	else
		disc.blocks = content_blocks;

	disc.head->blocks = disc.blocks;

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
	setup_anchor(&disc);
	setup_vds(&disc);
	setup_lvid(&disc, next_extent(disc.head, LVID));

	pspace = next_extent(disc.head, PSPACE);
	if (!pspace)
	{
		fprintf(stderr, "%s: Error: No partition space available\n", appname);
		close(fd);
		exit(1);
	}

	disc.metadata_start = 2;
	setup_fileset(&disc, pspace);
	setup_root(&disc, pspace);

	{
		struct udf_desc *root_desc = NULL;
		struct udf_desc *d = pspace->head;
		while (d)
		{
			if (d->ident == TAG_IDENT_EFE || d->ident == TAG_IDENT_FE)
			{
				struct extendedFileEntry *efe = (struct extendedFileEntry *)d->data->buffer;
				if (efe->icbTag.fileType == ICBTAG_FILE_TYPE_DIRECTORY)
				{
					root_desc = d;
					break;
				}
			}
			d = d->next;
		}

		if (!root_desc)
		{
			fprintf(stderr, "%s: Error: Cannot find root directory\n", appname);
			close(fd);
			exit(1);
		}

		uint32_t next_offset = root_desc->offset + 1;

		/* Calculate metadata_blocks BEFORE packing files (metadata = FSD + root dir only) */
		{
			uint32_t last_offset = 0;
			d = pspace->head;
			while (d)
			{
				uint32_t end = d->offset + (d->length + disc.blocksize - 1) / disc.blocksize;
				if (end > last_offset)
					last_offset = end;
				d = d->next;
			}
			disc.metadata_blocks = last_offset - disc.metadata_start;
		}

		printf("Packing files from: %s\n", source_dir);
		if (pack_directory(&disc, pspace, source_dir, root_desc, &next_offset) < 0)
		{
			close(fd);
			exit(1);
		}

		struct extendedFileEntry *root_efe = (struct extendedFileEntry *)root_desc->data->buffer;
		root_efe->descTag = query_tag(&disc, pspace, root_desc, 1);

		content_blocks = next_offset + 512;
		if (disc_capacity)
		{
			if (disc_capacity < content_blocks)
			{
				fprintf(stderr, "%s: Error: --disc-capacity (%"PRIu32") is smaller than needed content blocks (%"PRIu32")\n", appname, disc_capacity, content_blocks);
				close(fd);
				exit(1);
			}
			disc.blocks = disc_capacity;
		}
		else
		{
			disc.blocks = content_blocks;
		}

		disc.head->blocks = disc.blocks;
		if (ftruncate(fd, (off_t)disc.blocks * disc.blocksize) != 0)
		{
			fprintf(stderr, "%s: Error: Cannot resize file: %s\n", appname, strerror(errno));
			close(fd);
			exit(1);
		}

		disc.metadata_mirror_start = pspace->blocks - disc.metadata_blocks;
		setup_metadata(&disc, pspace);
	}

	setup_anchor(&disc);
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
