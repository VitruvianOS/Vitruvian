/*
 * Copyright 2002-2011 Haiku, Inc. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project, All rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NODE_H
#define _NODE_H

#include <OS.h>
#include <Statable.h>


class BDirectory;
class BEntry;
class BString;
struct entry_ref;


struct node_ref {
	node_ref();
	node_ref(dev_t device, ino_t node);
	//node_ref(vref_id id, uint32 flags);
	node_ref(int fd);
	node_ref(const node_ref& other);
	~node_ref();

	// This is experimental V\OS API. It will change.
	dev_t dev() const { return device; }
	ino_t ino() const { return node; }

	status_t init_check() const;

	vref_id id() const;
	bool is_virtual() const;
	const node_ref dereference() const;
	void unset();

	bool operator==(const node_ref& other) const;
	bool operator!=(const node_ref& other) const;
	bool operator<(const node_ref& other) const;
	node_ref& operator=(const node_ref& other);

#ifdef __VOS_NEW_ENTRY_REF__
protected:
#else
public:
#endif

	dev_t device;
	ino_t node;

private:
	team_id team;
};


class BNode : public BStatable {
public:
								BNode();
								BNode(const entry_ref* ref);
								BNode(const BEntry* entry);
								BNode(const char* path);
								BNode(const BDirectory* dir, const char* path);
								BNode(const BNode& node);
	virtual						~BNode();

			status_t			InitCheck() const;

	virtual	status_t			GetStat(struct stat* st) const;

			status_t			SetTo(const entry_ref* ref);
			status_t			SetTo(const BEntry* entry);
			status_t			SetTo(const char* path);
			status_t			SetTo(const BDirectory* dir, const char* path);
			void				Unset();

			status_t			Lock();
			status_t			Unlock();

			status_t			Sync();

			ssize_t				WriteAttr(const char* name, type_code type,
									off_t offset, const void* buffer,
									size_t length);
			ssize_t				ReadAttr(const char* name, type_code type,
									off_t offset, void* buffer,
									size_t length) const;
			status_t			RemoveAttr(const char* name);
			status_t			RenameAttr(const char* oldName,
									const char* newName);
			status_t			GetAttrInfo(const char* name,
									struct attr_info* info) const;
			status_t			GetNextAttrName(char* buffer);
			status_t			RewindAttrs();
			status_t			WriteAttrString(const char* name,
									const BString* data);
			status_t			ReadAttrString(const char* name,
									BString* result) const;

			status_t			GetNodeRef(node_ref* ref) const;

			BNode&				operator=(const BNode& node);
			bool				operator==(const BNode& node) const;
			bool				operator!=(const BNode& node) const;

			int					Dup() const;

private:
	friend class BFile;
	friend class BDirectory;
	friend class BSymLink;

	virtual	void				_RudeNode1();
	virtual	void				_RudeNode2();
	virtual	void				_RudeNode3();
	virtual	void				_RudeNode4();
	virtual	void				_RudeNode5();
	virtual	void				_RudeNode6();

private:
			status_t			set_fd(int fd);
	virtual	void				close_fd();
			void				set_status(status_t newStatus);

			status_t			_SetTo(int fd, const char* path, bool traverse);
			status_t			_SetTo(const entry_ref* ref, bool traverse);

	virtual	status_t			set_stat(struct stat& stat, uint32 what);

			status_t			_GetStat(struct stat* stat) const;
	virtual	status_t			_GetStat(struct stat_beos* stat) const;
			status_t			InitAttrDir();

private:
			uint32				rudeData[4];
			int					fFd;
				// file descriptor for the given node
			node_ref			fNodeRef;
			int					fAttrFd;
				// file descriptor for the attribute directory of the node,
				// initialized lazily
			status_t			fCStatus;
				// the node's initialization status
};


#endif	// _NODE_H
