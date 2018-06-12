#include <OS.h>

status_t		_kern_stop_notifying(port_id port, uint32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}

status_t		_kern_start_watching(dev_t device, ino_t node, uint32 flags,
						port_id port, uint32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t		_kern_stop_watching(dev_t device, ino_t node, port_id port,
						uint32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}

status_t _kstart_watching_vnode_(dev_t device, ino_t node,
											uint32 flags, port_id port,
											int32 handlerToken)
{
	UNIMPLEMENTED();
	return B_ERROR;
}

/*!	\brief Unsubscribes a target from watching a node.
	\param device The device the node resides on (node_ref::device).
	\param node The node ID of the node (node_ref::device).
	\param port The port of the target (a looper port).
	\param handlerToken The token of the target handler. \c -2, if the
		   preferred handler of the looper is the target.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
status_t _kstop_watching_vnode_(dev_t device, ino_t node,
										   port_id port, int32 handlerToken)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/*!	\brief Unsubscribes a target from node and mount monitoring.
	\param port The port of the target (a looper port).
	\param handlerToken The token of the target handler. \c -2, if the
		   preferred handler of the looper is the target.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
status_t _kstop_notifying_(port_id port, int32 handlerToken)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
