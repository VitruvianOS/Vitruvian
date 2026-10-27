/*
 ** Copyright 2019-2026, Dario Casalinuovo. All rights reserved
 ** Distributed under the terms of the LGPL License.
 */

#include <SupportDefs.h>

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include <linux/cdrom.h>

#include <device/scsi.h>

#include "syscalls.h"


#define CD_FRAME_SIZE	2352
#define CD_READ_CHUNK	8


static bool
is_cd_device(int fd)
{
	return ioctl(fd, CDROM_GET_CAPABILITY, 0) >= 0;
}


static status_t
cd_get_toc(int fd, scsi_toc* toc)
{
	struct cdrom_tochdr header;
	if (ioctl(fd, CDROMREADTOCHDR, &header) < 0)
		return -errno;

	memset(toc, 0, sizeof(*toc));

	int count = header.cdth_trk1 - header.cdth_trk0 + 1 + 1;
		// plus the lead-out
	if (count < 1 || count > (int)((sizeof(toc->toc_data) - 4) / 8))
		return B_BAD_DATA;

	uint16 length = 2 + count * 8;
	toc->toc_data[0] = length >> 8;
	toc->toc_data[1] = length & 0xff;
	toc->toc_data[2] = header.cdth_trk0;
	toc->toc_data[3] = header.cdth_trk1;

	for (int i = 0; i < count; i++) {
		struct cdrom_tocentry entry;
		memset(&entry, 0, sizeof(entry));
		entry.cdte_track = i == count - 1 ? CDROM_LEADOUT
			: header.cdth_trk0 + i;
		entry.cdte_format = CDROM_MSF;
		if (ioctl(fd, CDROMREADTOCENTRY, &entry) < 0)
			return -errno;

		uint8* out = toc->toc_data + 4 + i * 8;
		out[1] = (entry.cdte_adr << 4) | entry.cdte_ctrl;
		out[2] = entry.cdte_track;
		out[5] = entry.cdte_addr.msf.minute;
		out[6] = entry.cdte_addr.msf.second;
		out[7] = entry.cdte_addr.msf.frame;
	}

	return B_OK;
}


static status_t
cd_play_msf(int fd, const uint8 start[3], const uint8 end[3])
{
	struct cdrom_msf msf;
	msf.cdmsf_min0 = start[0];
	msf.cdmsf_sec0 = start[1];
	msf.cdmsf_frame0 = start[2];
	msf.cdmsf_min1 = end[0];
	msf.cdmsf_sec1 = end[1];
	msf.cdmsf_frame1 = end[2];
	return ioctl(fd, CDROMPLAYMSF, &msf) < 0 ? -errno : B_OK;
}


static status_t
cd_read(int fd, scsi_read_cd* read)
{
	int32 frames = (read->length_m * 60 + read->length_s) * 75
		+ read->length_f;

	if (read->play) {
		// absolute end = start + length
		int32 end = ((read->start_m * 60 + read->start_s) * 75
			+ read->start_f) + frames;
		uint8 start[3] = { read->start_m, read->start_s, read->start_f };
		uint8 stop[3] = { end / (60 * 75), (end / 75) % 60, end % 75 };
		return cd_play_msf(fd, start, stop);
	}

	if (read->buffer == NULL || read->buffer_length < CD_FRAME_SIZE)
		return B_BAD_VALUE;

	int32 maxFrames = read->buffer_length / CD_FRAME_SIZE;
	if (frames > maxFrames)
		frames = maxFrames;

	int32 position = (read->start_m * 60 + read->start_s) * 75
		+ read->start_f;
	int32 done = 0;
	while (done < frames) {
		int32 count = frames - done;
		if (count > CD_READ_CHUNK)
			count = CD_READ_CHUNK;

		int32 at = position + done;
		struct cdrom_read_audio audio;
		memset(&audio, 0, sizeof(audio));
		audio.addr.msf.minute = at / (60 * 75);
		audio.addr.msf.second = (at / 75) % 60;
		audio.addr.msf.frame = at % 75;
		audio.addr_format = CDROM_MSF;
		audio.nframes = count;
		audio.buf = (__u8*)read->buffer + done * CD_FRAME_SIZE;
		if (ioctl(fd, CDROMREADAUDIO, &audio) < 0)
			return -errno;

		done += count;
	}

	return B_OK;
}


static status_t
cd_get_position(int fd, scsi_position* position)
{
	struct cdrom_subchnl sub;
	memset(&sub, 0, sizeof(sub));
	sub.cdsc_format = CDROM_MSF;
	if (ioctl(fd, CDROMSUBCHNL, &sub) < 0)
		return -errno;

	// SCSI READ SUBCHANNEL, current position format
	memset(position, 0, sizeof(*position));
	uint8* data = position->position;
	data[1] = sub.cdsc_audiostatus;
	data[3] = 12;
	data[4] = 1;
	data[5] = (sub.cdsc_adr << 4) | sub.cdsc_ctrl;
	data[6] = sub.cdsc_trk;
	data[7] = sub.cdsc_ind;
	data[9] = sub.cdsc_absaddr.msf.minute;
	data[10] = sub.cdsc_absaddr.msf.second;
	data[11] = sub.cdsc_absaddr.msf.frame;
	data[13] = sub.cdsc_reladdr.msf.minute;
	data[14] = sub.cdsc_reladdr.msf.second;
	data[15] = sub.cdsc_reladdr.msf.frame;
	return B_OK;
}


// Maps the legacy SCSI CD opcodes onto the Linux cdrom ioctls.
// Returns false when cmd is not one of them or fd is not a CD device.
static bool
cd_ioctl(int fd, uint32 cmd, void* data, status_t* result)
{
	switch (cmd) {
		case B_SCSI_GET_TOC:
		case B_SCSI_READ_CD:
		case B_SCSI_PLAY_TRACK:
		case B_SCSI_PLAY_POSITION:
		case B_SCSI_STOP_AUDIO:
		case B_SCSI_PAUSE_AUDIO:
		case B_SCSI_RESUME_AUDIO:
		case B_SCSI_GET_POSITION:
		case B_SCSI_EJECT:
			break;

		default:
			return false;
	}

	if (!is_cd_device(fd))
		return false;

	bool needsData = cmd != B_SCSI_STOP_AUDIO && cmd != B_SCSI_PAUSE_AUDIO
		&& cmd != B_SCSI_RESUME_AUDIO && cmd != B_SCSI_EJECT;
	if (needsData && data == NULL) {
		*result = B_BAD_VALUE;
		return true;
	}

	switch (cmd) {
		case B_SCSI_GET_TOC:
			*result = cd_get_toc(fd, (scsi_toc*)data);
			break;

		case B_SCSI_READ_CD:
			*result = cd_read(fd, (scsi_read_cd*)data);
			break;

		case B_SCSI_PLAY_TRACK:
		{
			scsi_play_track* track = (scsi_play_track*)data;
			struct cdrom_ti ti;
			ti.cdti_trk0 = track->start_track;
			ti.cdti_ind0 = track->start_index;
			ti.cdti_trk1 = track->end_track;
			ti.cdti_ind1 = track->end_index;
			*result = ioctl(fd, CDROMPLAYTRKIND, &ti) < 0 ? -errno : B_OK;
			break;
		}

		case B_SCSI_PLAY_POSITION:
		{
			scsi_play_position* play = (scsi_play_position*)data;
			uint8 start[3] = { play->start_m, play->start_s, play->start_f };
			uint8 end[3] = { play->end_m, play->end_s, play->end_f };
			*result = cd_play_msf(fd, start, end);
			break;
		}

		case B_SCSI_STOP_AUDIO:
			*result = ioctl(fd, CDROMSTOP) < 0 ? -errno : B_OK;
			break;

		case B_SCSI_PAUSE_AUDIO:
			*result = ioctl(fd, CDROMPAUSE) < 0 ? -errno : B_OK;
			break;

		case B_SCSI_RESUME_AUDIO:
			*result = ioctl(fd, CDROMRESUME) < 0 ? -errno : B_OK;
			break;

		case B_SCSI_GET_POSITION:
			*result = cd_get_position(fd, (scsi_position*)data);
			break;

		case B_SCSI_EJECT:
			*result = ioctl(fd, CDROMEJECT) < 0 ? -errno : B_OK;
			break;
	}

	return true;
}


status_t
system_ioctl(int fd, uint32 cmd, void *data, size_t length)
{
	return _kern_ioctl(fd, cmd, data, length);
}


status_t
_kern_ioctl(int fd, uint32 cmd, void *data, size_t length)
{
	status_t result;
	if (cd_ioctl(fd, cmd, data, &result))
		return result;

	if (ioctl(fd, cmd, data, length) == 0)
		return B_OK;
	else
		return B_ERROR;
}
