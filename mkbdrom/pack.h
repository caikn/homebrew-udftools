#ifndef PACK_H
#define PACK_H

#include "libudffs.h"

struct file_entry {
	char *source_path;
	uint32_t data_start;
	uint64_t size;
	struct file_entry *next;
};

extern struct file_entry *file_list_head;

int pack_directory(struct udf_disc *disc, struct udf_extent *pspace, const char *source_path, struct udf_desc *parent_desc, uint32_t *next_offset);
int write_file_data(int fd, struct udf_disc *disc, struct udf_extent *pspace);

#endif /* PACK_H */
