//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  visopsys_io.c
//
//  A stdio-like disk I/O implementation for low-level disk access on
//  Visopsys.  Can access an NTFS volume while it is mounted.  Heavily
//  modified from win32_io.c in the Linux-NTFS project.
//
//  Copyrights in original file:
//    Copyright (c) 2003-2004 Lode Leroy
//    Copyright (c) 2003-2005 Anton Altaparmakov

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/api.h>
#include "device.h"

typedef struct {
  int isDisk;
  file file;
  disk disk;
  s64 partLength;
  s64 position;    // Logical current position in the file/disk

} visopsys_fd;

#define Vdebug(f, a...) do {                      \
  ntfs_log_debug("VISOPSYS: %s: ", __FUNCTION__); \
  ntfs_log_debug(f, ##a);                         \
} while(0)

static int ntfs_visopsys_errno(unsigned error)
{
  // Convert a Visopsys error to a UNIX one

  switch (error) {
  case ERR_NOSUCHFUNCTION:
    return EBADRQC;
  case ERR_NOSUCHENTRY:
  case ERR_NOSUCHFILE:
  case ERR_NOSUCHDIR:
    return ENOENT;
  case ERR_PERMISSION:
    return EACCES;
  case ERR_MEMORY:
    return ENOMEM;
  case ERR_NOFREE:
    return ENOSPC;
  case ERR_NOMEDIA:
    return ENODEV;
  case ERR_NOWRITE:
    return EROFS;
  case ERR_BUSY:
    return EBUSY;
  case ERR_INVALID:
  case ERR_NULLPARAMETER:
    return EINVAL;
  case ERR_NOSUCHDRIVER:
  case ERR_NOTIMPLEMENTED:
    return EOPNOTSUPP;
  default:
    // generic message
    return ERR_ERROR;
  }
}


static int ntfs_device_visopsys_open(struct ntfs_device *dev, int flags)
{
  // Open a device.  dev->d_name must hold the device name, the rest is
  // ignored.  Supported flags are O_RDONLY, O_WRONLY and O_RDWR.
  // If the name is not a Visopsys disk name, treat it as a file.

  int status = 0;
  file f;
  visopsys_fd *fd = NULL;

  Vdebug("OPEN\n");

  if (NDevOpen(dev))
    {
      errno = ntfs_visopsys_errno(ERR_BUSY);
      return (status = -1);
    }

  // File name?
  if (fileFind(dev->d_name, &f) >= 0)
    {
      ntfs_log_trace("Can't open regular files\n");
      errno = ntfs_visopsys_errno(ERR_NOTIMPLEMENTED);
      return (status = -1);
    }

  fd = malloc(sizeof(visopsys_fd));
  if (fd == NULL)
    {
      ntfs_log_trace("Memory allocation failure\n");
      errno = ntfs_visopsys_errno(ERR_MEMORY);
      return (status = -1);
    }

  // Disk name.  No UNIX-style opening required.

  // Try to get disk information
  status = diskGet(dev->d_name, &(fd->disk));
  if (status < 0)
    {
      ntfs_log_trace("Can't get disk information\n");
      goto error_out;
    }

  // Make sure it's a logical disk (a partition, that is) rather than a
  // physical one
  if ((fd->disk.type & DISKTYPE_LOGICALPHYSICAL) != DISKTYPE_LOGICAL)
    {
      ntfs_log_trace("Can't open physical disks\n");
      status = ERR_NOTIMPLEMENTED;
      goto error_out;
    }

  // Make sure the sector size is set
  if (fd->disk.sectorSize == 0)
    {
      ntfs_log_trace("Disk sector size is NULL\n");
      status = ERR_BUG;
      goto error_out;
    }

  fd->partLength = (fd->disk.numSectors * fd->disk.sectorSize);

  dev->d_private = fd;
  NDevSetBlock(dev);
  NDevSetOpen(dev);
  NDevClearDirty(dev);

  // Setup our read-only flag.
  if ((flags & O_RDWR) != O_RDWR)
    NDevSetReadOnly(dev);

  return (status = 0);

 error_out:
  free(fd);
  errno = ntfs_visopsys_errno(status);
  return (status = -1);
}


static int ntfs_device_visopsys_close(struct ntfs_device *dev)
{
  // Close an open ntfs deivce
  // dev:	ntfs device obtained via ->open
  // Return 0 if o.k.
  //	 -1 if not, and errno set.  Note if error fd->vol_handle is trashed.

  int status = 0;
  visopsys_fd *fd = NULL;

  Vdebug("CLOSE\n");

  // Check params
  if (dev == NULL)
    {
      ntfs_log_trace("NULL device parameter\n");
      errno = ntfs_visopsys_errno(ERR_NULLPARAMETER);
      return (status = -1);
    }

  if (!NDevOpen(dev))
    {
      ntfs_log_trace("Can't open device\n");
      errno = ntfs_visopsys_errno(ERR_INVALID);
      return (status = -1);
    }

  fd = (visopsys_fd *) dev->d_private;

  if (NDevDirty(dev))
    diskSync(fd->disk.name);

  NDevClearOpen(dev);
  dev->d_private = NULL;
  free(fd);

  return (status = 0);
}


static s64 ntfs_device_visopsys_seek(struct ntfs_device *dev, s64 offset,
				     int whence)
{
  // Change current logical file position
  // dev:	ntfs device obtained via ->open
  // offset:	required offset from the whence anchor
  // whence:	whence anchor specifying what @offset is relative to
  // Return the new position on the volume on success and -1 on error with
  // errno set to the error code.
  // whence may be one of the following:
  //	SEEK_SET - Offset is relative to file start.
  //	SEEK_CUR - Offset is relative to current position.
  //	SEEK_END - Offset is relative to end of file.

  s64 abs_ofs = 0;
  visopsys_fd *fd = NULL;

  //Vdebug("SEEK\n");

  // Check params
  if (dev == NULL)
    {
      ntfs_log_trace("NULL device parameter\n");
      errno = ntfs_visopsys_errno(ERR_NULLPARAMETER);
      return (abs_ofs = -1);
    }

  fd = (visopsys_fd *) dev->d_private;

  switch (whence)
    {
    case SEEK_SET:
      abs_ofs = offset;
      break;

    case SEEK_CUR:
      abs_ofs = (fd->position + offset);
      break;

    case SEEK_END:
      abs_ofs = (fd->partLength + offset);
      break;

    default:
      ntfs_log_trace("Invalid 'whence' seek argument %d\n", whence);
      errno = ntfs_visopsys_errno(ERR_INVALID);
      return (abs_ofs = -1);
    }

  // abs_ofs should be a multiple of the block size
  if (abs_ofs % (s64) fd->disk.sectorSize)
    {
      ntfs_log_trace("Seek address is not a multiple of sector size\n");
      errno = ntfs_visopsys_errno(ERR_INVALID);
      return (abs_ofs = -1);
    }

  if ((abs_ofs < 0) || (abs_ofs > fd->partLength))
    {
      ntfs_log_trace("Seek outside partition (sector %llu)",
	     (abs_ofs / (s64) fd->disk.sectorSize));
      if (abs_ofs < 0)
	ntfs_log_trace("(abs_ofs (%llu) < 0)\n", abs_ofs);
      else
	ntfs_log_trace("(abs_ofs (%llu) > fd->partLength (%llu))\n", abs_ofs,
		       fd->partLength);
      errno = EINVAL;
      return (abs_ofs = -1);
    }

  fd->position = abs_ofs;
  return (abs_ofs);
}


static s64 ntfs_device_visopsys_read(struct ntfs_device *dev, void *buff,
				     s64 count)
{
  // Read bytes from an ntfs device
  // dev:	ntfs device obtained via ->open
  // buff:	pointer to where to put the contents
  // count:	how many bytes should be read
  // On success returns the number of bytes actually read (can be < count).
  // On error returns -1 with errno set.

  int status = 0;
  visopsys_fd *fd = NULL;
  s64 startSector = 0;
  s64 sectorCount = 0;
  void *saveBuff = NULL;
  s64 br = 0;

  //Vdebug("READ\n");

  // Check params
  if ((dev == NULL) || (buff == NULL) || (count <= 0))
    {
      ntfs_log_trace("NULL parameter\n");
      errno = ntfs_visopsys_errno(ERR_NULLPARAMETER);
      return (status = -1);
    }

  fd = (visopsys_fd *) dev->d_private;

  startSector = (fd->position / (s64) fd->disk.sectorSize);
  sectorCount = (count / (s64) fd->disk.sectorSize);

  if ((fd->position % (s64) fd->disk.sectorSize) ||
      (count % (s64) fd->disk.sectorSize))
    {
      Vdebug("Doing off-kilter read\n");

      saveBuff = buff;

      if (fd->position % (s64) fd->disk.sectorSize)
	sectorCount += 1;

      if ((fd->position + count) % (s64) fd->disk.sectorSize)
	sectorCount += 1;

      buff = malloc(sectorCount * fd->disk.sectorSize);
      if (buff == NULL)
	{
	  ntfs_log_trace("Memory allocation failure\n");
	  errno = ntfs_visopsys_errno(ERR_MEMORY);
	  return (status = -1);
	}
    }

  // Read sectors
  status = diskReadSectors(fd->disk.name, startSector, sectorCount, buff);
  if (status < 0)
    {
      ntfs_log_trace("Error %d doing disk read\n", status);
      errno = ntfs_visopsys_errno(status);
      return (status = -1);
    }

  if (saveBuff)
    {
      memcpy(saveBuff, (buff + (fd->position % (s64) fd->disk.sectorSize)),
	     count);
      free(buff);
    }

  br = count;
  fd->position += count;
  return (br);
}


static s64 ntfs_device_visopsys_write(struct ntfs_device *dev,
				      const void *buff, s64 count)
{
  // Write bytes to an ntfs device
  // dev:	ntfs device obtained via ->open
  // buff:	pointer to the data to write
  // count:	how many bytes should be written
  // On success returns the number of bytes actually written.
  // On error returns -1 with errno set.

  int status = 0;
  visopsys_fd *fd = NULL;
  s64 startSector = 0;
  s64 sectorCount = 0;
  void *saveBuff = NULL;
  s64 br = 0;

  //Vdebug("WRITE\n");

  // Check params
  if ((dev == NULL) || (buff == NULL) || (count <= 0))
    {
      ntfs_log_trace("NULL parameter\n");
      errno = ntfs_visopsys_errno(ERR_NULLPARAMETER);
      return (status = -1);
    }

  if (NDevReadOnly(dev))
    {
      ntfs_log_trace("Device is read-only\n");
      errno = EROFS;
      return (status = -1);
    }

  fd = (visopsys_fd *) dev->d_private;

  startSector = (fd->position / (s64) fd->disk.sectorSize);
  sectorCount = (count / (s64) fd->disk.sectorSize);

  if ((fd->position % (s64) fd->disk.sectorSize) ||
      (count % (s64) fd->disk.sectorSize))
    {
      Vdebug("Doing off-kilter write\n");

      saveBuff = (void *) buff;

      if (fd->position % (s64) fd->disk.sectorSize)
	sectorCount += 1;

      if ((fd->position + count) % (s64) fd->disk.sectorSize)
	sectorCount += 1;

      buff = malloc(sectorCount * fd->disk.sectorSize);
      if (buff == NULL)
	{
	  ntfs_log_trace("Memory allocation failure\n");
	  errno = ntfs_visopsys_errno(ERR_MEMORY);
	  return (status = -1);
	}

      if (fd->position % (s64) fd->disk.sectorSize)
	{
	  // The current position is not a multiple of the sector size.
	  // Read the first sector into the buffer
	  status =
	    diskReadSectors(fd->disk.name, startSector, 1, (void *) buff);
	  if (status < 0)
	    {
	      ntfs_log_trace("Error %d doing disk read\n", status);
	      free((void *) buff);
	      errno = ntfs_visopsys_errno(status);
	      return (status = -1);
	    }
	}

      if ((fd->position + count) % (s64) fd->disk.sectorSize)
	{
	  // The count is not a multiple of the sector size.  Read the last
	  // sector into the buffer
	  s64 lastSector = (startSector + (sectorCount - 1));

	  status =
	    diskReadSectors(fd->disk.name, lastSector, 1, (void *)
			    (buff + ((lastSector - startSector) *
				     fd->disk.sectorSize)));
	  if (status < 0)
	    {
	      ntfs_log_trace("Error %d doing disk read\n", status);
	      free((void *) buff);
	      errno = ntfs_visopsys_errno(status);
	      return (status = -1);
	    }
	}

      // Copy the caller-supplied data into the appropriate place in the buffer
      memcpy((void *) (buff + (fd->position % (s64) fd->disk.sectorSize)),
	     saveBuff, count);
    }

  NDevSetDirty(dev);

  // Write sectors
  status = diskWriteSectors(fd->disk.name, startSector, sectorCount, buff);

  if (saveBuff)
    free((void *) buff);

  if (status < 0)
    {
      ntfs_log_trace("Error %d doing disk write\n", status);
      errno = ntfs_visopsys_errno(status);
      return (status = -1);
    }

  br = count;
  fd->position += count;
  return (br);
}


static s64 ntfs_device_visopsys_pread(struct ntfs_device *dev, void *b,
				      s64 count, s64 offset)
{
  //Vdebug("PREAD\n");
  return (ntfs_pread(dev, offset, count, b));
}


static s64 ntfs_device_visopsys_pwrite(struct ntfs_device *dev, const void *b,
				       s64 count, s64 offset)
{
  //Vdebug("PWRITE\n");
  NDevSetDirty(dev);
  return (ntfs_pwrite(dev, offset, count, b));
}


static int ntfs_device_visopsys_sync(struct ntfs_device *dev)
{
  // Flush write buffers to disk
  // dev:	ntfs device obtained via ->open
  // Return 0 if o.k.
  //	 -1 if not, and errno set.

  int status = 0;
  visopsys_fd *fd = NULL;

  Vdebug("SYNC\n");

  // Check params
  if (dev == NULL)
    {
      ntfs_log_trace("NULL device parameter\n");
      errno = ntfs_visopsys_errno(ERR_NULLPARAMETER);
      return (status = -1);
    }

  fd = (visopsys_fd *) dev->d_private;

  if (!NDevReadOnly(dev) && NDevDirty(dev))
    {
      status = diskSync(fd->disk.name);
      if (status < 0)
	{
	  ntfs_log_trace("Error syncing disk\n");
	  errno = ntfs_visopsys_errno(errno);
	  return (status = -1);
	}

      NDevClearDirty(dev);
    }

  return (status = 0);
}


static int ntfs_device_visopsys_stat(struct ntfs_device *dev,
				     struct stat *buff)
{
  // Get a unix-like stat structure for an ntfs device
  // dev:	ntfs device obtained via ->open
  // buf:	pointer to the stat structure to fill
  // Note: Only st_mode, st_size, and st_blocks are filled.
  // Return 0 if o.k.
  //	 -1 if not and errno set. in this case handle is trashed.

  int status = 0;
  //visopsys_fd *fd = NULL;

  Vdebug("STAT\n");

  // Check params
  if ((dev == NULL) || (buff == NULL))
    {
      errno = ntfs_visopsys_errno(ERR_NULLPARAMETER);
      return (status = -1);
    }

  //fd = (visopsys_fd *) dev->d_private;

  ntfs_log_trace("stat() operation not implemented\n");
  errno = ntfs_visopsys_errno(ERR_NOTIMPLEMENTED);
  return (status = -1);
}


static int ntfs_device_visopsys_ioctl(struct ntfs_device *dev, int request,
				      void *argp)
{
  int status = 0;
  visopsys_fd *fd = NULL;

  Vdebug("IOCTL %x\n", request);

  // Check params
  if ((dev == NULL) || (argp == NULL))
    {
      ntfs_log_trace("NULL parameter\n");
      errno = ntfs_visopsys_errno(ERR_NULLPARAMETER);
      return (status = -1);
    }

  fd = (visopsys_fd *) dev->d_private;

  switch (request)
    {
    case BLKGETSIZE:
      // Get the size of the device in sectors
      *((int *) argp) = fd->disk.numSectors;
      break;

    case BLKGETSIZE64:
      // Get the size of the device in bytes
      *((s64 *) argp) = fd->partLength;
      break;

    case HDIO_GETGEO:
      ((struct hd_geometry *) argp)->heads = fd->disk.heads;
      ((struct hd_geometry *) argp)->sectors = fd->disk.sectorsPerCylinder;
      ((struct hd_geometry *) argp)->cylinders = fd->disk.cylinders;
      ((struct hd_geometry *) argp)->start = 0;
      break;

    case BLKSSZGET:
      *((int *) argp) = fd->disk.sectorSize;
      break;

    case BLKBSZSET:
      // Set the device sector size.  Not applicable.
      break;

    default:
      ntfs_log_trace("IOCTL %x not implemented\n", request);
      errno = ntfs_visopsys_errno(ERR_NOTIMPLEMENTED);
      return (status = -1);
    }
  return 0;
}


struct ntfs_device_operations ntfs_device_visopsys_io_ops = {
  .open		= ntfs_device_visopsys_open,
  .close	= ntfs_device_visopsys_close,
  .seek		= ntfs_device_visopsys_seek,
  .read		= ntfs_device_visopsys_read,
  .write	= ntfs_device_visopsys_write,
  .pread	= ntfs_device_visopsys_pread,
  .pwrite	= ntfs_device_visopsys_pwrite,
  .sync		= ntfs_device_visopsys_sync,
  .stat		= ntfs_device_visopsys_stat,
  .ioctl	= ntfs_device_visopsys_ioctl
};
