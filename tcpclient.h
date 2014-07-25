/***************************************
* @file     tcpclient.h
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
		    2014-07-24  phata  ��tcpsocket�з����TCPClient��
			                   �����߳�ʵ��libuv��run(�¼�ѭ��)���κ�libuv��ز������ڴ��߳�����ɡ����TCPClient���Զ��߳����������
							   һ��client��Ҫ4��BUFFER_SIZE(readbuffer_,writebuffer_,writebuf_list_,readpacket_),���������߳�(readpacket_�ڲ�һ����Connect����һ��)
****************************************/
#ifndef TCPCLIENT_H
#define TCPCLIENT_H
#include <string>
#include <list>
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
/**********************************************�ͻ���****************************************************/
/*************************************************
���ܣ�TCP �ͻ��˶���
���÷�����
���ûص�����SetRecvCB/SetClosedCB
����Connect/Connect6���������ͻ���
����Send��������(��ѡ)
����Closeֹͣ�ͻ��ˣ�����ֹͣʱ�ᴥ���ڻص�SetRecvCB�������õĺ���
����IsClosed�жϿͻ����Ƿ������ر���
*************************************************/
class TCPClient
{
public:
    TCPClient(char packhead, char packtail);
    virtual ~TCPClient();
    static void StartLog(const char* logpath = nullptr);//������־�����������Ż�������־
public:
    //��������
    void SetRecvCB(ClientRecvCB pfun, void* userdata);////���ý��ջص�������ֻ��һ��
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//���ý��չر��¼��Ļص�����
    bool Connect(const char* ip, int port);//����connect�̣߳�ѭ���ȴ�ֱ��connect���
    bool Connect6(const char* ip, int port);//����connect�̣߳�ѭ���ȴ�ֱ��connect���
    int  Send(const char* data, std::size_t len);
	void Close(){ isuseraskforclosed_ = true;}//�û��رտͻ��ˣ�IsClosed����true���������ر���
	bool IsClosed(){return isclosed_;};//�жϿͻ����Ƿ��ѹر�
    //�Ƿ�����Nagle�㷨
    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
protected:
    bool init();//��ʼ������
	void closeinl();//�ڲ�������������
    bool run(int status = UV_RUN_DEFAULT);//�����¼�ѭ��
    static void ConnectThread(void* arg);//������connect�߳�

    static void AfterConnect(uv_connect_t* handle, int status);
    static void AfterRecv(uv_stream_t *client, ssize_t nread, const uv_buf_t* buf);
    static void AfterSend(uv_write_t *req, int status);
    static void AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void AfterClientClose(uv_handle_t *handle);
    static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//������һ֡��Ļص�����
	static void PrepareCB(uv_prepare_t* handle);//prepare�׶λص�
	static void CheckCB(uv_check_t* handle);//check�׶λص�
	static void IdleCB(uv_idle_t* handle);//idle�׶λص�

private:
    enum {
        CONNECT_TIMEOUT,
        CONNECT_FINISH,
        CONNECT_ERROR,
        CONNECT_DIS,
    };
    uv_tcp_t client_handle_;//�ͻ�������
	uv_idle_t idle_handle_;//���н׶�handle,�ݲ�ʹ��
	uv_prepare_t prepare_handle_;//prepare�׶�handle,���ڼ��ر����Ƿ���Ҫ��������
	uv_check_t check_handle_;//check�׶�handle,�ݲ�ʹ��
    uv_loop_t loop_;
    bool isclosed_;//�Ƿ��ѹر�
	bool isuseraskforclosed_;//�û��Ƿ�������ر�

	uv_thread_t connect_threadhandle_;//�����߳�
    uv_connect_t connect_req_;//����ʱ����

    int connectstatus_;//����״̬

    //�������ݲ���
    uv_buf_t readbuffer_;//�������ݵ�buf
    Packet readpacket_;//����buf���ݽ����ɰ�

    //�������ݲ���
	uv_buf_t writebuffer_;//�������ݵ�buf
    uv_mutex_t mutex_writebuf_;//����writebuf_list_
    std::list<uv_write_t*> writereq_list_;//���õ�uv_write_t
    uv_mutex_t mutex_writereq_;//����writereq_list_
    PodCircularBuffer<char> writebuf_list_;//�������ݶ���

    ClientRecvCB recvcb_;//�ص�����
    void* recvcb_userdata_;//�ص��������û�����

    TcpCloseCB closedcb_;//�رպ�ص���TCPServer
    void *closedcb_userdata_;

    std::string connectip_;//���ӵķ�����IP
    int connectport_;//���ӵķ������˿ں�

    std::string errmsg_;//������Ϣ

    char PACKET_HEAD;//��ͷ
    char PACKET_TAIL;//��β
};
}


#endif // TCPCLIENT_H