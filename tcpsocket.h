/***************************************
* @file     tcpsocket.h
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
****************************************/
#ifndef TCPSocket_H
#define TCPSocket_H
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
class TCPServer;
class AcceptClient;

typedef void (*tcp_closed)(int clientid, void* userdata);//tcp handle�رպ�ص����ϲ�
typedef void (*newconnect)(int clientid);//TCPServer���յ��¿ͻ��˻ص����û�
typedef void (*server_recvcb)(int clientid, const NetPacket& packethead, const unsigned char* buf, void* userdata);//TCPServer���յ��ͻ������ݻص����û�
typedef void (*client_recvcb)(const NetPacket& packethead, const unsigned char* buf, void* userdata);//TCPClient���յ����������ݻص����û�

typedef struct _sendparam_ {
    uv_async_t async;
    uv_sem_t semt;
    char* data;
    int len;
} sendparam;

/************************************************************���������Ŀͻ�������***************************************************************************/
/*************************************************
���ܣ�TCP Server Accept�л�ȡ��client����
���÷�����
���ûص�����SetRecvCB/SetClosedCB
newһ����(����ʹ��ֱ�Ӷ������ķ���)
����AcceptByServer
����send��������(��ѡ)
����run����(��ѡ����loop�������ط��Ѿ�run��������Ҫ����)
����Stopֹͣ�����߳�
�ڻص�closedcb_��delete����
*************************************************/
class AcceptClient
{
public:
    AcceptClient(int clientid, char packhead, char packtail, uv_loop_t* loop = uv_default_loop());
    virtual ~AcceptClient();
    bool Start(char packhead, char packtail);
    void Stop();
    bool AcceptByServer(uv_tcp_t* server);

    void SetRecvCB(server_recvcb pfun, void* userdata);//���ý������ݻص�����
    void SetClosedCB(tcp_closed pfun, void* userdata);//���ý��չر��¼��Ļص�����

    int Send(const char* data, std::size_t len);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
private:
    uv_loop_t* loop_;

    bool isinit_;
    int client_id_;//�ͻ���id,Ωһ,��TCPServer����ֵ
    uv_tcp_t client_handle_;//�ͻ��˾��

    server_recvcb recvcb_;//�������ݻص����û��ĺ���
    void *recvcb_userdata_;

    tcp_closed closedcb_;//�رպ�ص���TCPServer
    void *closedcb_userdata_;

    //�������ݲ���
    uv_buf_t readbuffer_;//�������ݵ�buf
    Packet readpacket_;//����buf���ݽ����ɰ�

    //�������ݲ���
    uv_thread_t writethread_handle_;//�����߳̾��
    bool is_writethread_stop_;//���Ʒ����߳��˳�
    uv_mutex_t mutex_writebuf_;//����writebuf_list_
    PodCircularBuffer<char> writebuf_list_;//�������ݶ���
    sendparam sendparam_;
    std::list<uv_write_t*> writereq_list_;//���õ�uv_write_t
    uv_mutex_t mutex_writereq_;//����writereq_list_

    std::string errmsg_;//���������Ϣ
private:
    static void WriteThread(void *arg);//���������߳�
    static void AsyncSendCB(uv_async_t* handle);//�첽��������
    static void AfterSend(uv_write_t *req, int status);
    static void AfterRecv(uv_stream_t *client, ssize_t nread, const uv_buf_t* buf);
    static void AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void AfterClientClose(uv_handle_t *handle);
    static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//������һ�������ݰ��ص����û�
};



/****************************************************************������**********************************************************************************/
/*************************************************
���ܣ�TCP Server
���÷�����
���ûص�����setnewconnectcb/setrecvcb

����StartLog������־����(��ѡ)
����Start/Start6����������
����send��������(��ѡ)
����closeֹͣ�����߳�
*************************************************/
class TCPServer
{
public:
    TCPServer(char packhead, char packtail, uv_loop_t* loop = uv_default_loop());
    virtual ~TCPServer();
    static void StartLog(const char* logpath = nullptr);//������־�����������Ż�������־
public:
    //��������
    bool Start(const char *ip, int port);//����������,��ַΪIP4
    bool Start6(const char *ip, int port);//��������������ַΪIP6
    void Close();

    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };

    int  Send(int clientid, const char* data, std::size_t len);
    void SetNewConnectCB(newconnect cb);
    void SetRecvCB(int clientid,server_recvcb cb, void *userdata);//���ý��ջص�������ÿ���ͻ��˸���һ��
protected:
    int GetAvailaClientID()const;//��ȡ���õ�client id
    //��̬�ص�����
    static void AfterServerClose(uv_handle_t *handle);
    static void AcceptConnection(uv_stream_t *server, int status);
    static void SubClientClosed(int clientid,void* userdata);//AcceptClient�رպ�ص���TCPServer

private:
    bool init();
    bool run(int status = UV_RUN_DEFAULT);
    bool bind(const char* ip, int port);
    bool bind6(const char* ip, int port);
    bool listen(int backlog = SOMAXCONN);

    uv_loop_t *loop_;
    uv_tcp_t server_;//����������
    bool isinit_;//�Ƿ��ѳ�ʼ��������close�������ж�
    std::map<int,AcceptClient*> clients_list_;//�ӿͻ�������
    uv_mutex_t mutex_clients_;//����clients_list_

    newconnect newconcb_;//�ص�����
    std::string errmsg_;//������Ϣ

    char PACKET_HEAD;//��ͷ
    char PACKET_TAIL;//��β
};


/***********************************************************************�ͻ���***************************************************************************/
/*************************************************
���ܣ�TCP Server Accept�л�ȡ��client����
���÷�����
���ûص�����SetRecvCB/SetClosedCB
newһ����(����ʹ��ֱ�Ӷ������ķ���)
����Connect/Connect6����
����send��������(��ѡ)
����Closeֹͣ�����߳�
�ڻص�closedcb_��delete����
*************************************************/
class TCPClient
{
public:
    TCPClient(char packhead, char packtail, uv_loop_t* loop = uv_default_loop());
    virtual ~TCPClient();
    static void StartLog(const char* logpath = nullptr);//������־�����������Ż�������־
public:
    //��������
    bool Connect(const char* ip, int port);//����connect�̣߳�ѭ���ȴ�ֱ��connect���
    bool Connect6(const char* ip, int port);//����connect�̣߳�ѭ���ȴ�ֱ��connect���
    int  Send(const char* data, std::size_t len);
    void SetRecvCB(client_recvcb pfun, void* userdata);////���ý��ջص�������ֻ��һ��
	void SetClosedCB(tcp_closed pfun, void* userdata);//���ý��չر��¼��Ļص�����
    void Close();

    //�Ƿ�����Nagle�㷨
    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
protected:
    bool init();
    bool run(int status = UV_RUN_DEFAULT);
    static void ConnectThread(void* arg);//������connect�߳�
    static void ConnectThread6(void* arg);//������connect�߳�
    
	static void WriteThread(void *arg);//���������߳�
	static void AsyncSendCB(uv_async_t* handle);//�첽��������
    static void AfterConnect(uv_connect_t* handle, int status);
    static void AfterRecv(uv_stream_t *client, ssize_t nread, const uv_buf_t* buf);
    static void AfterSend(uv_write_t *req, int status);
    static void AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void AfterClientClose(uv_handle_t *handle);
	static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//������һ֡��Ļص�����

private:
    enum {
        CONNECT_TIMEOUT,
        CONNECT_FINISH,
        CONNECT_ERROR,
        CONNECT_DIS,
    };
    uv_tcp_t client_handle_;//�ͻ�������
    uv_loop_t *loop_;
    bool isinit_;//�Ƿ��ѳ�ʼ��������close�������ж�
	uv_thread_t connect_threadhandle_;

    uv_connect_t connect_req_;//����ʱ����

    int connectstatus_;//����״̬

    //�������ݲ���
    uv_buf_t readbuffer_;//�������ݵ�buf
    Packet readpacket_;//����buf���ݽ����ɰ�

    //�������ݲ���
    uv_thread_t writethread_handle_;//�����߳̾��
    bool is_writethread_stop_;//���Ʒ����߳��˳�
    uv_mutex_t mutex_writebuf_;//����writebuf_list_
    PodCircularBuffer<char> writebuf_list_;//�������ݶ���
    sendparam sendparam_;
    std::list<uv_write_t*> writereq_list_;//���õ�uv_write_t
    uv_mutex_t mutex_writereq_;//����writereq_list_

    client_recvcb recvcb_;//�ص�����
    void* recvcb_userdata_;//�ص��������û�����

	tcp_closed closedcb_;//�رպ�ص���TCPServer
	void *closedcb_userdata_;

    std::string connectip_;//���ӵķ�����IP
    int connectport_;//���ӵķ������˿ں�

    std::string errmsg_;//������Ϣ

    char PACKET_HEAD;//��ͷ
    char PACKET_TAIL;//��β
};

/***********************************************************************��������***************************************************************************/
/*****************************
* @brief   ��������ϳ�NetPacket��ʽ�Ķ�����������ֱ�ӷ��͡�
* @param   packet --NetPacket���������version,header,tail,type,datalen,reserve������ǰ��ֵ���ú��������check��ֵ��Ȼ����ϳɶ�����������
	       data   --Ҫ���͵�ʵ������
* @return  std::string --���صĶ�����������ַ��&string[0],���ȣ�string.length()
******************************/
std::string PacketData(NetPacket& packet, const unsigned char* data);
}


#endif // TCPSocket_H