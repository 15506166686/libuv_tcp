#include "tcpclient.h"
#include "log4z.h"

namespace uv
{
/*****************************************TCP Client*************************************************************/
TCPClient::TCPClient(char packhead, char packtail)
    :PACKET_HEAD(packhead), PACKET_TAIL(packtail)
    ,recvcb_(nullptr),recvcb_userdata_(nullptr),closedcb_(nullptr),closedcb_userdata_(nullptr)
    ,connectstatus_(CONNECT_DIS),writebuf_list_(BUFFER_SIZE)
    , isclosed_(true), isuseraskforclosed_(false)
{
    int iret = uv_loop_init(&loop_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        fprintf(stdout,"init loop error: %s\n",errmsg_.c_str());
    }
    readbuffer_ = uv_buf_init((char*) malloc(BUFFER_SIZE), BUFFER_SIZE);
    writebuffer_ = uv_buf_init((char*) malloc(BUFFER_SIZE), BUFFER_SIZE);
    connect_req_.data = this;
}


TCPClient::~TCPClient()
{
    closeinl();
    if (readbuffer_.base) {
        free(readbuffer_.base);
        readbuffer_.base = NULL;
        readbuffer_.len = 0;
    }
    if (writebuffer_.base) {
        free(writebuffer_.base);
        writebuffer_.base = NULL;
        writebuffer_.len = 0;
    }
    uv_loop_close(&loop_);
    LOGI("�ͻ���("<<this<<")�˳�");
}
//��ʼ����ر�--��������ͻ���һ��
bool TCPClient::init()
{
    if (!isclosed_) {
        return true;
    }
    int iret = uv_prepare_init(&loop_, &prepare_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_prepare_start(&prepare_handle_, PrepareCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    prepare_handle_.data = this;

    iret = uv_check_init(&loop_, &check_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_check_start(&check_handle_, CheckCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    check_handle_.data = this;

    iret = uv_idle_init(&loop_, &idle_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_idle_start(&idle_handle_, IdleCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    idle_handle_.data = this;

    iret = uv_tcp_init(&loop_,&client_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    client_handle_.data = this;

    //��ʼ��write�̲߳���
    iret = uv_mutex_init(&mutex_writereq_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_mutex_init(&mutex_writebuf_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }

    readpacket_.SetPacketCB(GetPacket,this);
    readpacket_.Start(PACKET_HEAD,PACKET_TAIL);
    //iret = uv_tcp_keepalive(&client_, 1, 60);//
    //if (iret) {
    //    errmsg_ = GetUVError(iret);
    //    return false;
    //}
    LOGI("�ͻ���("<<this<<")Init");
    isclosed_ = false;
    return true;
}

void TCPClient::closeinl()
{
    if (isclosed_) {
        return;
    }
    readpacket_.Stop();
	uv_mutex_destroy(&mutex_writebuf_);

    uv_mutex_lock(&mutex_writereq_);
    for (auto it= writereq_list_.begin(); it!=writereq_list_.end(); ++it) {
        free(*it);
    }
    writereq_list_.clear();
    uv_mutex_unlock(&mutex_writereq_);
    uv_mutex_destroy(&mutex_writereq_);

    uv_close((uv_handle_t*)&client_handle_,AfterClientClose);//����close���AfterClientClose����������close,��ͨ��IsClosed�ж��Ƿ�ر�
    uv_close((uv_handle_t*)&check_handle_,AfterClientClose);//����close���AfterClientClose����������close,��ͨ��IsClosed�ж��Ƿ�ر�
    uv_close((uv_handle_t*)&prepare_handle_,AfterClientClose);//����close���AfterClientClose����������close,��ͨ��IsClosed�ж��Ƿ�ر�
    uv_close((uv_handle_t*)&idle_handle_,AfterClientClose);//����close���AfterClientClose����������close,��ͨ��IsClosed�ж��Ƿ�ر�
    LOGI("�ͻ���("<<this<<")close");
}

bool TCPClient::run(int status)
{
    int iret = uv_run(&loop_,(uv_run_mode)status);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

//��������--��������ͻ���һ��
bool TCPClient::setNoDelay(bool enable)
{
    //http://blog.csdn.net/u011133100/article/details/21485983
    int iret = uv_tcp_nodelay(&client_handle_, enable ? 1 : 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

bool TCPClient::setKeepAlive(int enable, unsigned int delay)
{
    int iret = uv_tcp_keepalive(&client_handle_, enable , delay);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

//��Ϊclient��connect����
bool TCPClient::Connect(const char* ip, int port)
{
    closeinl();
    init();
    connectip_ = ip;
    connectport_ = port;
	struct sockaddr_in bind_addr;
	int iret = uv_ip4_addr(connectip_.c_str(), connectport_, &bind_addr);
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE(errmsg_);
		return false;
	}
	iret = uv_tcp_connect(&connect_req_, &client_handle_, (const sockaddr*)&bind_addr, AfterConnect);
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE(errmsg_);
		return false;
	}

	LOGI("�ͻ���("<<this<<")start connect to server("<<ip<<":"<<port<<")");
    iret = uv_thread_create(&connect_threadhandle_, ConnectThread, this);//����AfterConnect�����������ӳɹ����������߳�
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    int wait_count = 0;
    while ( connectstatus_ == CONNECT_DIS) {
        //fprintf(stdout,"client(%p) wait, connect status %d\n",this,connectstatus_);
#if defined (WIN32) || defined(_WIN32)
        Sleep(100);
#else
        usleep((100) * 1000);
#endif
        if(++wait_count > 100) {
            connectstatus_ = CONNECT_TIMEOUT;
            break;
        }
    }
    return connectstatus_ == CONNECT_FINISH;
}

bool TCPClient::Connect6(const char* ip, int port)
{
    closeinl();
    init();
    connectip_ = ip;
    connectport_ = port;
	struct sockaddr_in6 bind_addr;
	int iret = uv_ip6_addr(connectip_.c_str(), connectport_, &bind_addr);
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE(errmsg_);
		return false;
	}
	iret = uv_tcp_connect(&connect_req_, &client_handle_, (const sockaddr*)&bind_addr, AfterConnect);
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE(errmsg_);
		return false;
	}
    
	LOGI("�ͻ���("<<this<<")start connect to server("<<ip<<":"<<port<<")");
    iret = uv_thread_create(&connect_threadhandle_, ConnectThread, this);//����AfterConnect�����������ӳɹ����������߳�
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    int wait_count = 0;
    while ( connectstatus_ == CONNECT_DIS) {
        //fprintf(stdout,"client(%p) wait, connect status %d\n",this,connectstatus_);
#if defined (WIN32) || defined(_WIN32)
        Sleep(100);
#else
        usleep((100) * 1000);
#endif
        if(++wait_count > 100) {
            connectstatus_ = CONNECT_TIMEOUT;
            break;
        }
    }
    return connectstatus_ == CONNECT_FINISH;
}

void TCPClient::ConnectThread( void* arg )
{
    TCPClient *pclient = (TCPClient*)arg;
    pclient->run();
}

void TCPClient::AfterConnect(uv_connect_t* handle, int status)
{
    //fprintf(stdout,"start after connect\n");
    TCPClient *theclass = (TCPClient*)handle->handle->data;
    if (status) {
        theclass->connectstatus_ = CONNECT_ERROR;
        LOGE("�ͻ���("<<theclass<<") connect error:"<<GetUVError(status));
        fprintf(stdout,"connect error:%s\n",GetUVError(status));
        return;
    }

    int iret = uv_read_start(handle->handle, AllocBufferForRecv, AfterRecv);//�ͻ��˿�ʼ���շ�����������
    if (iret) {
        LOGE("�ͻ���("<<theclass<<") uv_read_start error:"<<GetUVError(status));
        fprintf(stdout,"uv_read_start error:%s\n",GetUVError(iret));
        theclass->connectstatus_ = CONNECT_ERROR;
    } else {
        theclass->connectstatus_ = CONNECT_FINISH;
    }
    LOGI("�ͻ���("<<theclass<<")run");
    //fprintf(stdout,"end after connect\n");
}

//�ͻ��˵ķ��ͺ���
int TCPClient::Send(const char* data, std::size_t len)
{
    if (!data || len <= 0) {
        errmsg_ = "send data is null or len less than zero.";
        LOGE(errmsg_);
        return 0;
    }
    size_t iret = 0;
    while(1) {
        uv_mutex_lock(&mutex_writebuf_);
        iret +=writebuf_list_.write(data+iret,len-iret);
        uv_mutex_unlock(&mutex_writebuf_);
        if (iret < len) {
            ThreadSleep(100);
            continue;
        } else {
            break;
        }
    }
    return iret;
}

//�ͻ���-�������ݻص�����
void TCPClient::SetRecvCB(ClientRecvCB pfun, void* userdata )
{
    recvcb_ = pfun;
    recvcb_userdata_ = userdata;
}

void TCPClient::SetClosedCB( TcpCloseCB pfun, void* userdata )
{
    //��AfterRecv����
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}

//�ͻ��˷����ռ亯��
void TCPClient::AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    if (!handle->data) {
        return;
    }
    TCPClient *theclass = (TCPClient*)handle->data;
    *buf = theclass->readbuffer_;
}


void TCPClient::AfterRecv(uv_stream_t *handle, ssize_t nread, const uv_buf_t* buf)
{
    if (!handle->data) {
        return;
    }
    TCPClient *theclass = (TCPClient*)handle->data;//��������recv������TCPClient
    if (nread < 0) {
        if (nread == UV_EOF) {
            fprintf(stdout,"�����������Ͽ�,ClientΪ%p\n",handle);
            LOGW("�����������Ͽ�");
        } else if (nread == UV_ECONNRESET) {
            fprintf(stdout,"�������쳣�Ͽ�,ClientΪ%p\n",handle);
            LOGW("�������쳣�Ͽ�");
        } else {
            fprintf(stdout,"�������쳣�Ͽ���,ClientΪ%p:%s\n",handle,GetUVError(nread));
            LOGW("�������쳣�Ͽ�"<<GetUVError(nread));
        }
        theclass->closeinl();
        return;
    }
    if (nread > 0) {
        //client->recvcb_(buf->base,nread,client->userdata_);//�ɷ�ʽ-�ص�������
        theclass->readpacket_.recvdata((const unsigned char*)buf->base,nread);//�·�ʽ-����������ٻص�����
    }
}

//��������ͻ���һ��
void TCPClient::AfterSend(uv_write_t *req, int status)
{
    //����req
    TCPClient *theclass =(TCPClient*)req->data;
    uv_mutex_lock(&theclass->mutex_writereq_);
    theclass->writereq_list_.push_back(req);
    uv_mutex_unlock(&theclass->mutex_writereq_);

    if (status < 0) {
        LOGE("������������:"<<GetUVError(status));
        fprintf(stderr, "send error %s\n", GetUVError(status));
    }
}

void TCPClient::AfterClientClose( uv_handle_t *handle )
{
    TCPClient *theclass = (TCPClient*)handle->data;
    fprintf(stdout,"Close CB handle %p\n",handle);
    if (handle == (uv_handle_t *)&theclass->prepare_handle_) {
        handle->data = 0;//��ֵ0�������ж��Ƿ���ù�
    }
    if (handle == (uv_handle_t *)&theclass->check_handle_) {
        handle->data = 0;//��ֵ0�������ж��Ƿ���ù�
    }
    if (handle == (uv_handle_t *)&theclass->client_handle_) {
        handle->data = 0;//��ֵ0�������ж��Ƿ���ù�
    }
    if (handle == (uv_handle_t *)&theclass->idle_handle_) {
        handle->data = 0;//��ֵ0�������ж��Ƿ���ù�
    }
    if (theclass->prepare_handle_.data == 0
        && theclass->client_handle_.data == 0
        && theclass->check_handle_.data == 0
        && theclass->idle_handle_.data == 0) {
        theclass->isclosed_ = true;
        LOGI("client  had closed.");
        if (theclass->closedcb_) {//֪ͨTCPServer�˿ͻ����Ѿ��ر�
            theclass->closedcb_(-1,theclass->closedcb_userdata_);
        }
    }
}

void TCPClient::StartLog( const char* logpath /*= nullptr*/ )
{
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerMonthdir(LOG4Z_MAIN_LOGGER_ID, true);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerDisplay(LOG4Z_MAIN_LOGGER_ID,false);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerLevel(LOG4Z_MAIN_LOGGER_ID,LOG_LEVEL_DEBUG);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerLimitSize(LOG4Z_MAIN_LOGGER_ID,100);
    if (logpath) {
        zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerPath(LOG4Z_MAIN_LOGGER_ID,logpath);
    }
    zsummer::log4z::ILog4zManager::GetInstance()->Start();
}

void TCPClient::GetPacket( const NetPacket& packethead, const unsigned char* packetdata, void* userdata )
{
    if (!userdata) {
        return;
    }
    TCPClient *theclass = (TCPClient*)userdata;
    if (theclass->recvcb_) {//�ѵõ������ݻص����û�
        theclass->recvcb_(packethead,packetdata,theclass->recvcb_userdata_);
    }
}

void TCPClient::PrepareCB( uv_prepare_t* handle )
{
    /////////////////////////prepare�׶μ���û��Ƿ��͹ر�����Ƿ�������Ҫ����
    TCPClient *theclass = (TCPClient*)handle->data;
    //����Ƿ�ر�
    if (theclass->isuseraskforclosed_) {
        theclass->closeinl();
        theclass->isuseraskforclosed_ = false;
        return;
    }
    //����Ƿ�������Ҫ����
    uv_mutex_lock(&theclass->mutex_writebuf_);
    if(theclass->writebuf_list_.empty()) {
        uv_mutex_unlock(&theclass->mutex_writebuf_);
        return;//û������Ҫ���ͣ��˳�
    }
    theclass->writebuffer_.len = theclass->writebuf_list_.read(theclass->writebuffer_.base,BUFFER_SIZE);//�õ�Ҫ���͵�����
    uv_mutex_unlock(&theclass->mutex_writebuf_);

    //��ȡ���õ�uv_write_t
    uv_write_t *req = NULL;
    uv_mutex_lock(&theclass->mutex_writereq_);
    if (theclass->writereq_list_.empty()) {
        uv_mutex_unlock(&theclass->mutex_writereq_);
        req = (uv_write_t*)malloc(sizeof(*req));
        req->data = theclass;
    } else {
        req = theclass->writereq_list_.front();
        theclass->writereq_list_.pop_front();
        uv_mutex_unlock(&theclass->mutex_writereq_);
    }
    int iret = uv_write((uv_write_t*)req, (uv_stream_t*)&theclass->client_handle_, &theclass->writebuffer_, 1, AfterSend);//����
    if (iret) {
        theclass->writereq_list_.push_back(req);//����ʧ�ܣ��������AfterSend���������Եû���req
        LOGE("�ͻ���("<<theclass<<") send error:"<<GetUVError(iret));
        fprintf(stdout,"send error. %s-%s\n",uv_err_name(iret),uv_strerror(iret));
    }
}

void TCPClient::CheckCB( uv_check_t* handle )
{
    TCPClient *theclass = (TCPClient*)handle->data;
    //check�׶���ʱ�������κ�����
}

void TCPClient::IdleCB( uv_idle_t* handle )
{
    TCPClient *theclass = (TCPClient*)handle->data;
    //check�׶���ʱ�������κ�����
}
}