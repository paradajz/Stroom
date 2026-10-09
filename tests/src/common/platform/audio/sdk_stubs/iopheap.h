/**
 * @brief Substitute SifInitIopHeap in this host fixture.
 */
int SifInitIopHeap(void);

/**
 * @brief Substitute SifAllocIopHeap in this host fixture.
 */
void* SifAllocIopHeap(int bytes);

/**
 * @brief Substitute SifFreeIopHeap in this host fixture.
 */
int SifFreeIopHeap(void* address);
