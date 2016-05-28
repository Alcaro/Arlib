#ifdef __linux__
#include "sandbox-internal.h"
#undef bind
 #include <sys/types.h> /* See NOTES */
#include <sys/socket.h>
#include <string.h>

void sandbox_cross_init(int* pfd, int* cfd)
{
	int fds[2];
	socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
	*pfd = fds[0];
	*cfd = fds[1];
}

//from http://blog.varunajayasiri.com/passing-file-descriptors-between-processes-using-sendmsg-and-recvmsg
//somewhat reformatted
void sandbox_cross_send(int socket, int fd_to_send)
{
	/* We are passing at least one byte of data so that recvmsg() will not return 0 */
	struct iovec iov = { (char*)" ", 1 };
	char ctrl_buf[CMSG_SPACE(sizeof(int))] = {};
	struct msghdr message = {
		.msg_name = NULL, .msg_namelen = 0,
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = ctrl_buf, .msg_controllen = sizeof(ctrl_buf),
		.msg_flags = 0,
	};
	
	cmsghdr* ctrl_msg = CMSG_FIRSTHDR(&message);
	ctrl_msg->cmsg_level = SOL_SOCKET;
	ctrl_msg->cmsg_type = SCM_RIGHTS;
	ctrl_msg->cmsg_len = CMSG_LEN(sizeof(int));
	*(int*)CMSG_DATA(ctrl_msg) = fd_to_send;
	
	sendmsg(socket, &message, 0);
}

int sandbox_cross_recv(int socket)
{
	char data;
	struct iovec iov = { &data, 1 };
	char ctrl_buf[CMSG_SPACE(sizeof(int))] = {};
	struct msghdr message = {
		.msg_name = NULL, .msg_namelen = 0,
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = ctrl_buf, .msg_controllen = sizeof(ctrl_buf),
		.msg_flags = 0,
	};
	
	if (recvmsg(socket, &message, 0) < 0) return -1;
	
	/* Iterate through header to find if there is a file descriptor */
	for (cmsghdr* ctrl_msg=CMSG_FIRSTHDR(&message);ctrl_msg!=NULL;ctrl_msg=CMSG_NXTHDR(&message, ctrl_msg))
	{
		if (ctrl_msg->cmsg_level == SOL_SOCKET && ctrl_msg->cmsg_type == SCM_RIGHTS)
		{
			return *(int*)CMSG_DATA(ctrl_msg);
		}
	}
	
	return -1;
}
#endif
