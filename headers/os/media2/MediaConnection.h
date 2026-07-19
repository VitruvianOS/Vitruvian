/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_CONNECTION_H
#define _MEDIA2_MEDIA_CONNECTION_H


#include <String.h>

#include <media2/MediaClientDefs.h>
#include <media2/MediaFormat.h>


class BMediaClient;


class BMediaConnection {
public:
			media_connection_id		Id() const;
			const char*				Name() const;
			BMediaClient*			Client() const;

			bool					IsConnected() const;
			bool					HasBinding() const;
			BMediaConnection*		Binding() const;

			const BMediaFormat&		Format() const;
			status_t				SetAcceptedFormat(const BMediaFormat& format);

			status_t				Disconnect();
			status_t				Release();

	virtual	size_t					BufferSize() const = 0;
	virtual	void					SetBufferSize(size_t size);

protected:
									BMediaConnection(media_connection_kinds kinds,
										const char* name = NULL);
	virtual							~BMediaConnection();

	virtual	void					Connected(const BMediaFormat& format);
	virtual	void					Disconnected();

private:
			media_connection_id		fId;
			BString					fName;
			media_connection_kinds	fKinds;
			BMediaClient*			fOwner;
			BMediaConnection*		fBinding;
			BMediaFormat			fFormat;
			BMediaFormat			fAcceptedFormat;
			size_t					fBufferSize;
			bool					fConnected;

	friend class BMediaClient;
};


class BMediaInput : public virtual BMediaConnection {
public:
									BMediaInput(const char* name = NULL);
	virtual							~BMediaInput();

	virtual	size_t					BufferSize() const override;

protected:
	virtual	status_t				AcceptFormat(BMediaFormat* format) = 0;
	virtual	void					HandleBuffer(void* buffer, size_t size,
										const BMediaFormat& format);
	virtual	void					Connected(const BMediaFormat& format) override;
	virtual	void					Disconnected() override;

	friend class BMediaClient;
};


class BMediaOutput : public virtual BMediaConnection {
public:
									BMediaOutput(const char* name = NULL);
	virtual							~BMediaOutput();

	virtual	size_t					BufferSize() const override;

	virtual	status_t				SendBuffer(void* buffer, size_t size);

protected:
	virtual	status_t				PrepareToConnect(BMediaFormat* format) = 0;
	virtual	status_t				FormatProposal(BMediaFormat* format) = 0;
	virtual	void					Connected(const BMediaFormat& format) override;
	virtual	void					Disconnected() override;

private:
			bool					fEnabled;
			uint64					fFramesSent;

	friend class BMediaClient;
};


#endif // _MEDIA2_MEDIA_CONNECTION_H
