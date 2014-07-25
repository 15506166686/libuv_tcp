/***************************************
* @file     tcpserver.h
* @brief    ����libuv��װ��tcp��������ͻ���,ʹ��log4z����־����
* @details
* @author   �¼���, wqvbjhc@gmail.com
* @date     2014-05-13
* @mod      2014-05-13  phata  ������������ͻ��˵Ĵ���.�ַ�����֧�ֶ�ͻ�������
                               �޸Ŀͻ��˲��Դ��룬֧�ֲ�����ͻ��˲���
			2014-05-23  phata  ԭ��������ͻ���ֻ���������ݣ��ָ�Ϊ����NetPacket(����net_base.h)��װ�����ݡ����ջص�Ϊ����������ݣ���������Ҫ�û��Լ���ճ�NetPacket����
			                   �޸�server_recvcb�Ķ��壬���NetPacket����
							   �޸�client_recvcb�Ķ��壬���NetPacket����
							   ����uv_write_t�б�ռ�����send
			2014-05-27  phata  clientdata����ΪAcceptClient�����ḻ���书��.
			                   ʹ���첽���ͻ��ƣ������������߳��е��÷�������ͻ��˵�send����
							   �޸�֮ǰ���Է��ֱ��������
							   BUFFER_SIZE��1M��Ϊ10K��һ��client��Ҫ6��BUFFER_SIZE.һ��client�ڲ�������2���߳�
			2014-07-24  phata  ��tcpsocket�з����TCPServer��
							   �����߳�ʵ��libuv��run(�¼�ѭ��)���κ�libuv��ز������ڴ��߳�����ɡ����TCPServer���Զ��߳����������
							   һ��client��Ҫ4��BUFFER_SIZE(readbuffer_,writebuffer_,writebuf_list_,readpacket_),����һ���߳�(readpacket_�ڲ�һ��)
****************************************/
#ifndef TCPSERVER_H
#define TCPSERVER_H
#include <string>
#include <list>
#include <map>
#include "uv.h"
#include "net/packet.h"
#ifndef BUFFER_SIZE
#define BUFFER_SIZE (1024*10)
#endif

//#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
//
//#define container_of(ptr, type, member) \
//	((type *) ((char *) (ptr) - offsetof(type, member)))

namespace uv
{
/***********************************************���������Ŀͻ�������**********************************************************************/
/*************************************************
���ܣ�TCP Server Accept�л�ȡ��client����
���÷�����
���ûص�����SetRecvCB/SetClosedCB
����AcceptByServer�����Ϸ�����
����Send��������(��ѡ)
����Closeֹͣ�ͻ��ˣ�����ֹͣʱ�ᴥ���ڻص�closedcb_��delete����SetClosedCB�����õĺ���
����IsClosed�жϿͻ����Ƿ������ر���
*************************************************/
class AcceptClient
{
public:
	AcceptClient(int clientid, char packhead, char packtail, uv_loop_t* loop);//server��loop
    virtual ~AcceptClient();

    void SetRecvCB(ServerRecvCB pfun, void* userdata);//���ý������ݻص�����
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//���ý��չر��¼��Ļص�����
	int Send(const char* data, std::size_t len);
	void Close(){ isuseraskforclosed_ = true;}//�û��رտͻ��ˣ�IsClosed����true���������ر���
	bool IsClosed(){return isclosed_;};//�жϿͻ����Ƿ��ѹر�
    bool AcceptByServer(uv_tcp_t* server);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
private:
	bool init(char packhead, char packtail);
	void closeinl();//�ڲ�������������

	uv_loop_t* loop_;//server������loop
    int client_id_;//�ͻ���id,Ωһ,��TCPServer����ֵ
    uv_tcp_t client_handle_;//�ͻ��˾��
	uv_prepare_t prepare_handle_;//prepare�׶�handle,���ڼ��ر����Ƿ���Ҫ��������
	bool isclosed_;//�Ƿ��ѹر�
	bool isuseraskforclosed_;//�û��Ƿ�������ر�

    //�������ݲ���
    uv_buf_t readbuffer_;//�������ݵ�buf
    Packet readpacket_;//����buf���ݽ����ɰ�

	//�������ݲ���
	uv_buf_t writebuffer_;//�������ݵ�buf
	uv_mutex_t mutex_writebuf_;//����writebuf_list_
	std::list<uv_write_t*> writereq_list_;//���õ�uv_write_t
	uv_mutex_t mutex_writereq_;//����writereq_list_
	PodCircularBuffer<char> writebuf_list_;//�������ݶ���

    std::string errmsg_;//���������Ϣ

    ServerRecvCB recvcb_;//�������ݻص����û��ĺ���
    void *recvcb_userdata_;

    TcpCloseCB closedcb_;//�رպ�ص���TCPServer
    void *closedcb_userdata_;
private:
    static void AfterSend(uv_write_t *req, int status);
    static void AfterRecv(uv_stream_t *client, ssize_t nread, const uv_buf_t* buf);
    static void AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void AfterClientClose(uv_handle_t *handle);
    static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//������һ�������ݰ��ص����û�
	static void PrepareCB(uv_prepare_t* handle);//prepare�׶λص�
};



/***************************************************************������*******************************************************************************/
/*************************************************
���ܣ�TCP Server
���÷�����
���ûص�����SetNewConnectCB/SetRecvCB
����StartLog������־����(��ѡ)
����Start/Start6����������
����Send��������(��ѡ)
����Closeֹͣ������������ֹͣʱ�ᴥ���ڻص�SetRecvCB�������õĺ���
����IsClosed�жϿͻ����Ƿ������ر���
*************************************************/
class TCPServer
{
public:
    TCPServer(char packhead, char packtail);
    virtual ~TCPServer();
    static void StartLog(const char* logpath = nullptr);//������־�����������Ż�������־
public:
    //��������
    void SetNewConnectCB(NewConnectCB cb, void *userdata);
    void SetRecvCB(int clientid,ServerRecvCB cb, void *userdata);//���ý��ջص�������ÿ���ͻ��˸���һ��
	void SetClosedCB(TcpCloseCB pfun, void* userdata);//���ý��չر��¼��Ļص�����
    bool Start(const char *ip, int port);//����������,��ַΪIP4
    bool Start6(const char *ip, int port);//��������������ַΪIP6
	int  Send(int clientid, const char* data, std::size_t len);
	void Close(){ isuseraskforclosed_ = true;}//�û��رտͻ��ˣ�IsClosed����true���������ر���
	bool IsClosed(){return isclosed_;};//�жϿͻ����Ƿ��ѹر�

	//�Ƿ�����Nagle�㷨
    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };

protected:
    int GetAvailaClientID()const;//��ȡ���õ�client id
    //��̬�ص�����
    static void AfterServerClose(uv_handle_t *handle);
    static void AcceptConnection(uv_stream_t *server, int status);
    static void SubClientClosed(int clientid,void* userdata);//AcceptClient�رպ�ص���TCPServer
	static void PrepareCB(uv_prepare_t* handle);//prepare�׶λص�
	static void CheckCB(uv_check_t* handle);//check�׶λص�
	static void IdleCB(uv_idle_t* handle);//idle�׶λص�

private:
	enum {
		START_TIMEOUT,
		START_FINISH,
		START_ERROR,
		START_DIS,
	};

    bool init();
	void closeinl();//�ڲ�������������
    bool run(int status = UV_RUN_DEFAULT);
    bool bind(const char* ip, int port);
    bool bind6(const char* ip, int port);
    bool listen(int backlog = SOMAXCONN);

    uv_loop_t loop_;
    uv_tcp_t server_handle_;//����������
	uv_idle_t idle_handle_;//���н׶�handle,�ݲ�ʹ��
	uv_prepare_t prepare_handle_;//prepare�׶�handle,���ڼ��ر����Ƿ���Ҫ��������
	uv_check_t check_handle_;//check�׶�handle,�ݲ�ʹ��
	bool isclosed_;//�Ƿ��ѳ�ʼ��������close�������ж�
	bool isuseraskforclosed_;//�û��Ƿ�������ر�

    std::map<int,AcceptClient*> clients_list_;//�ӿͻ�������
    uv_mutex_t mutex_clients_;//����clients_list_

	uv_thread_t start_threadhandle_;//�����߳�
	static void StartThread(void* arg);//������Start�߳�,һֱ����ֱ���û������˳�
	int startstatus_;//����״̬

    std::string errmsg_;//������Ϣ

    NewConnectCB newconcb_;//�ص�����
	void *newconcb_userdata_;

	TcpCloseCB closedcb_;//�رպ�ص���TCPServer
	void *closedcb_userdata_;

	std::string serverip_;//���ӵ�IP
	int serverport_;//���ӵĶ˿ں�

    char PACKET_HEAD;//��ͷ
    char PACKET_TAIL;//��β
};
}


#endif // TCPSERVER_H