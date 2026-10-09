#pragma once

/**
 * @brief Substitute _ps2sdk_ioctl in this host fixture.
 */
int _ps2sdk_ioctl(int fd, int request, void* data);
