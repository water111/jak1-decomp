#ifndef SCE_IOP_MCMAN_BLOCK_H
#define SCE_IOP_MCMAN_BLOCK_H

struct McBadBlockState {
  int port, slot, block;
};
extern struct McBadBlockState mcman_badblock_state;
extern int mcman_replacement_clusters[16];
extern int mcman_probe_port, mcman_probe_slot, mcman_probe_block, mcman_probe_result;
int mcman_writecluster(int port, int slot, int cluster, int test);
int mcman_reportBadBlocks(int port, int slot);
int mcman_write_superblock(int port, int slot);

int mcman_checkBackupBlocks(int port, int slot);

#endif
