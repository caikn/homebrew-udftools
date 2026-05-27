#ifndef PACK_H
#define PACK_H

#include <sys/types.h>

#include "libudffs.h"

struct file_entry {
	char *source_path;
	struct udf_desc *desc;
	uint32_t data_start;
	uint64_t size;
	dev_t device;
	ino_t inode;
	struct file_entry *next;
};

extern struct file_entry *file_list_head;

void set_pack_progress(struct udf_disc *disc, const char *label, uint64_t total);
int pack_directory(struct udf_disc *disc, struct udf_extent *pspace, const char *source_path, struct udf_desc *parent_desc, uint32_t *next_offset);
uint32_t layout_file_data(struct udf_disc *disc, struct udf_extent *pspace, uint32_t start_offset);
void reset_file_entries(void);
int write_file_data(int fd, struct udf_disc *disc, struct udf_extent *pspace);

#endif /* PACK_H */
