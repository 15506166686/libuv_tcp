#include "tcpsocket.h"
#include "log4z.h"

std::string GetUVError(int retcode)
{
    std::string err;
    err = uv_err_name(retcode);
    err +=":";
    err += uv_strerror(retcode);
    return std::move(err);
}


namespace uv
{
/*****************************************TCP Server*************************************************************/
TCPServer::TCPServer(char packhead, char packtail, uv_loop_t* loop)
    :PACKET_HEAD(packhead), PACKET_TAIL(packtail), loop_(loop)
    ,newconcb_(nullptr), isinit_(false)
{

}


TCPServer::~TCPServer()
{
    Close();
    LOGI("tcp server exit.");
}

//��ʼ����ر�--��������ͻ���һ��
bool TCPServer::init()
{
    if (isinit_) {
        return true;
    }

    if (!loop_) {
        errmsg_ = "loop is null on tcp init.";
        LOGE(errmsg_);
        return false;
    }
    int iret = uv_mutex_init(&mutex_clients_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }

    iret = uv_tcp_init(loop_,&server_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    server_.data = this;

    iret = uv_tcp_nodelay(&server_,  1);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    //iret = uv_tcp_keepalive(&server_, 1, 60);//���ô˺����������������ó���
    //if (iret) {
    //	errmsg_ = GetUVError(iret);
    //	return false;
    //}
    isinit_ = true;
    return true;
}

void TCPServer::Close()
{
    if (!isinit_) {
        return;
    }

    uv_mutex_lock(&mutex_clients_);
    for (auto it = clients_list_.begin(); it!=clients_list_.end(); ++it) {
        auto data = it->second;
        data->Stop();
    }
    //clients_list_.clear();
    uv_mutex_unlock(&mutex_clients_);

    uv_close((uv_handle_t*) &server_, AfterServerClose);
    LOGI("close server");
    uv_mutex_destroy(&mutex_clients_);
    isinit_ = false;
}

bool TCPServer::run(int status)
{
    LOGI("server runing.");
    int iret = uv_run(loop_,(uv_run_mode)status);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}
//��������--��������ͻ���һ��
bool TCPServer::setNoDelay(bool enable)
{
    int iret = uv_tcp_nodelay(&server_, enable ? 1 : 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

bool TCPServer::setKeepAlive(int enable, unsigned int delay)
{
    int iret = uv_tcp_keepalive(&server_, enable , delay);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

//��Ϊserverʱ�ĺ���
bool TCPServer::bind(const char* ip, int port)
{
    struct sockaddr_in bind_addr;
    int iret = uv_ip4_addr(ip, port, &bind_addr);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_tcp_bind(&server_, (const struct sockaddr*)&bind_addr,0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    //iret = uv_tcp_keepalive(&server_, 1, 60);//���ô˺����������������ó���
    //if (iret) {
    //	errmsg_ = GetUVError(iret);
    //	return false;
    //}
    LOGI("server bind ip="<<ip<<", port="<<port);
    return true;
}

bool TCPServer::bind6(const char* ip, int port)
{
    struct sockaddr_in6 bind_addr;
    int iret = uv_ip6_addr(ip, port, &bind_addr);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_tcp_bind(&server_, (const struct sockaddr*)&bind_addr,0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    LOGI("server bind ip="<<ip<<", port="<<port);
    return true;
}

bool TCPServer::listen(int backlog)
{
    int iret = uv_listen((uv_stream_t*) &server_, backlog, AcceptConnection);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    LOGI("server Start listen. Runing.......");
    fprintf(stdout,"Server Runing.......\n");
    return true;
}

bool TCPServer::Start( const char *ip, int port )
{
    Close();
    if (!init()) {
        return false;
    }
    if (!bind(ip,port)) {
        return false;
    }
    if (!listen(SOMAXCONN)) {
        return false;
    }
    if (!run()) {
        return false;
    }
    return true;
}

bool TCPServer::Start6( const char *ip, int port )
{
    Close();
    if (!init()) {
        return false;
    }
    if (!bind6(ip,port)) {
        return false;
    }
    if (!listen(SOMAXCONN)) {
        return false;
    }
    if (!run()) {
        return false;
    }
    return true;
}

//���������ͺ���
int TCPServer::Send(int clientid, const char* data, std::size_t len)
{
    uv_mutex_lock(&mutex_clients_);
    auto itfind = clients_list_.find(clientid);
    if (itfind == clients_list_.end()) {
        uv_mutex_unlock(&mutex_clients_);
        errmsg_ = "can't find cliendid ";
        errmsg_ += std::to_string((long long)clientid);
        LOGE(errmsg_);
        return -1;
    }
    itfind->second->Send(data,len);
    uv_mutex_unlock(&mutex_clients_);
    return 1;
}

//������-�¿ͻ��˺���
void TCPServer::AcceptConnection(uv_stream_t *server, int status)
{

    if (!server->data) {
        return;
    }
    TCPServer *tcpsock = (TCPServer *)server->data;
    if (status) {
        tcpsock->errmsg_ = GetUVError(status);
        LOGE(tcpsock->errmsg_);
        return;
    }
    int clientid = tcpsock->GetAvailaClientID();
    AcceptClient* cdata = new AcceptClient(clientid, tcpsock->PACKET_HEAD, tcpsock->PACKET_TAIL);//uv_close�ص��������ͷ�
    uv_mutex_lock(&tcpsock->mutex_clients_);
    tcpsock->clients_list_.insert(std::make_pair(clientid,cdata));//���뵽���Ӷ���
    uv_mutex_unlock(&tcpsock->mutex_clients_);
    cdata->SetClosedCB(SubClientClosed,tcpsock);
    if(!cdata->AcceptByServer((uv_tcp_t*)server)) {
        tcpsock->errmsg_ = cdata->GetLastErrMsg();
        LOGE(tcpsock->errmsg_);
        cdata->Stop();
        return;
    }
    //fprintf(stdout,"Client handle %p, client id %d\n",&cdata->client_handle,clientid);
    if (tcpsock->newconcb_) {
        tcpsock->newconcb_(clientid);
    }
    LOGI("new client id="<< clientid);
    return;
}

//������-�������ݻص�����
void TCPServer::SetRecvCB(int clientid, server_recvcb cb, void *userdata)
{
    uv_mutex_lock(&mutex_clients_);
    auto itfind = clients_list_.find(clientid);
    if (itfind != clients_list_.end()) {
        itfind->second->SetRecvCB(cb,userdata);
    }
    uv_mutex_unlock(&mutex_clients_);
}



//������-�����ӻص�����
void TCPServer::SetNewConnectCB(newconnect cb )
{
    newconcb_ = cb;
}

void TCPServer::AfterServerClose(uv_handle_t *handle)
{
    //������,����Ҫ��ʲô
}

int TCPServer::GetAvailaClientID() const
{
    static int s_id = 0;
    return ++s_id;
}



void TCPServer::StartLog( const char* logpath /*= nullptr*/ )
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

void TCPServer::SubClientClosed( int clientid, void* userdata )
{
    TCPServer *server = (TCPServer*)userdata;
    uv_mutex_lock(&server->mutex_clients_);
    auto itfind = server->clients_list_.find(clientid);
    if (itfind != server->clients_list_.end()) {
        delete itfind->second;
        LOGI("ɾ���ͻ���:"<<itfind->first);
        fprintf(stdout,"ɾ���ͻ��ˣ�%d\n",itfind->first);
        server->clients_list_.erase(itfind);
    }
    uv_mutex_unlock(&server->mutex_clients_);
}



/*****************************************TCP Client*************************************************************/
TCPClient::TCPClient(char packhead, char packtail, uv_loop_t* loop)
    :PACKET_HEAD(packhead), PACKET_TAIL(packtail), loop_(loop)
    ,recvcb_(nullptr),recvcb_userdata_(nullptr),closedcb_(nullptr),closedcb_userdata_(nullptr)
    ,connectstatus_(CONNECT_DIS),writebuf_list_(BUFFER_SIZE)
    , isinit_(false),is_writethread_stop_(true)
{
    readbuffer_ = uv_buf_init((char*) malloc(BUFFER_SIZE), BUFFER_SIZE);
    connect_req_.data = this;
}


TCPClient::~TCPClient()
{
    if (readbuffer_.base) {
        free(readbuffer_.base);
        readbuffer_.base = NULL;
        readbuffer_.len = 0;
    }
    LOGI("�ͻ���("<<this<<")�˳�");
}
//��ʼ����ر�--��������ͻ���һ��
bool TCPClient::init()
{
    if (isinit_) {
        return true;
    }

    if (!loop_) {
        errmsg_ = "loop is null on tcp init.";
        LOGE(errmsg_);
        return false;
    }
    int iret = uv_tcp_init(loop_,&client_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    client_handle_.data = this;

    //��ʼ��async �ص�
    iret = uv_async_init(loop_,&sendparam_.async,AsyncSendCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    sendparam_.async.data = this;
    iret = uv_sem_init(&sendparam_.semt,0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    //uv_sem_post(&sendparam_.semt);

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
    is_writethread_stop_ = false;
    iret = uv_thread_create(&writethread_handle_,WriteThread,this);
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
    isinit_ = true;
    return true;
}

void TCPClient::Close()
{
    if (!isinit_) {
        return;
    }
    readpacket_.Stop();
    is_writethread_stop_ = true;
    uv_sem_post(&sendparam_.semt);//�����źţ���writethread�߳��л����˳�
    int iret = uv_thread_join(&writethread_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        fprintf(stdout,"thread join error, %s-%s\n",uv_err_name(iret),uv_strerror(iret));
    }
    uv_sem_destroy(&sendparam_.semt);
    uv_mutex_destroy(&mutex_writebuf_);

    uv_mutex_lock(&mutex_writereq_);
    for (auto it= writereq_list_.begin(); it!=writereq_list_.end(); ++it) {
        free(*it);
    }
    writereq_list_.clear();
    uv_mutex_unlock(&mutex_writereq_);
    uv_mutex_destroy(&mutex_writereq_);

    uv_close((uv_handle_t*)&sendparam_.async,AfterClientClose);
    uv_close((uv_handle_t*)&client_handle_,AfterClientClose);
    uv_thread_join(&connect_threadhandle_);
    LOGI("�ͻ���("<<this<<")close");
    isinit_ = false;
}

bool TCPClient::run(int status)
{
    LOGI("�ͻ���("<<this<<")run");
    int iret = uv_run(loop_,(uv_run_mode)status);
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
    Close();
    init();
    connectip_ = ip;
    connectport_ = port;
    LOGI("�ͻ���("<<this<<")start connect to server("<<ip<<":"<<port<<")");
    int iret = uv_thread_create(&connect_threadhandle_, ConnectThread, this);//����AfterConnect�����������ӳɹ����������߳�
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    while ( connectstatus_ == CONNECT_DIS) {
#if defined (WIN32) || defined(_WIN32)
        Sleep(100);
#else
        usleep((100) * 1000)
#endif
    }
    return connectstatus_ == CONNECT_FINISH;
}

bool TCPClient::Connect6(const char* ip, int port)
{
    Close();
    init();
    connectip_ = ip;
    connectport_ = port;
    LOGI("�ͻ���("<<this<<")start connect to server("<<ip<<":"<<port<<")");
    uv_thread_t connect_threadhanlde;
    int iret = uv_thread_create(&connect_threadhanlde, ConnectThread6, this);//����AfterConnect�����������ӳɹ����������߳�
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    uv_thread_join(&connect_threadhanlde);
    while ( connectstatus_ == CONNECT_DIS) {
        //fprintf(stdout,"client(%p) wait, connect status %d\n",this,connectstatus_);
#if defined (WIN32) || defined(_WIN32)
        Sleep(100);
#else
        usleep((100) * 1000)
#endif
    }
    return connectstatus_ == CONNECT_FINISH;
}

void TCPClient::ConnectThread( void* arg )
{
    TCPClient *pclient = (TCPClient*)arg;
    //fprintf(stdout,"client(%p) ConnectThread start\n",pclient);
    struct sockaddr_in bind_addr;
    int iret = uv_ip4_addr(pclient->connectip_.c_str(), pclient->connectport_, &bind_addr);
    if (iret) {
        pclient->errmsg_ = GetUVError(iret);
        LOGE(pclient->errmsg_);
        return;
    }
    iret = uv_tcp_connect(&pclient->connect_req_, &pclient->client_handle_, (const sockaddr*)&bind_addr, AfterConnect);
    if (iret) {
        pclient->errmsg_ = GetUVError(iret);
        LOGE(pclient->errmsg_);
        return;
    }
    //fprintf(stdout,"client(%p) ConnectThread end, connect status %d\n",pclient, pclient->connectstatus_);
    pclient->run();
}


void TCPClient::ConnectThread6( void* arg )
{
    TCPClient *pclient = (TCPClient*)arg;
    //fprintf(stdout,"client(%p) ConnectThread start\n",pclient);
    struct sockaddr_in6 bind_addr;
    int iret = uv_ip6_addr(pclient->connectip_.c_str(), pclient->connectport_, &bind_addr);
    if (iret) {
        pclient->errmsg_ = GetUVError(iret);
        LOGE(pclient->errmsg_);
        return;
    }
    iret = uv_tcp_connect(&pclient->connect_req_, &pclient->client_handle_, (const sockaddr*)&bind_addr, AfterConnect);
    if (iret) {
        pclient->errmsg_ = GetUVError(iret);
        LOGE(pclient->errmsg_);
        return;
    }
    //fprintf(stdout,"client(%p) ConnectThread end, connect status %d\n",pclient, pclient->connectstatus_);
    pclient->run();
}

void TCPClient::AfterConnect(uv_connect_t* handle, int status)
{
    //fprintf(stdout,"start after connect\n");
    TCPClient *pclient = (TCPClient*)handle->handle->data;
    if (status) {
        pclient->connectstatus_ = CONNECT_ERROR;
        LOGE("�ͻ���("<<pclient<<") connect error:"<<GetUVError(status));
        fprintf(stdout,"connect error:%s\n",GetUVError(status));
        return;
    }

    int iret = uv_read_start(handle->handle, AllocBufferForRecv, AfterRecv);//�ͻ��˿�ʼ���շ�����������
    if (iret) {
        LOGE("�ͻ���("<<pclient<<") uv_read_start error:"<<GetUVError(status));
        fprintf(stdout,"uv_read_start error:%s\n",GetUVError(iret));
        pclient->connectstatus_ = CONNECT_ERROR;
    } else {
        pclient->connectstatus_ = CONNECT_FINISH;
    }
    LOGI("�ͻ���("<<pclient<<")run");
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

//д�����߳�
void TCPClient::WriteThread( void *arg )
{
    TCPClient *theclass =(TCPClient*)arg;
    sendparam *param = &theclass->sendparam_;
    param->data =(char*)malloc(BUFFER_SIZE);
    param->len = BUFFER_SIZE;
    while(!theclass->is_writethread_stop_) {
        uv_mutex_lock(&theclass->mutex_writebuf_);
        if(theclass->writebuf_list_.empty()) {
            uv_mutex_unlock(&theclass->mutex_writebuf_);
            ThreadSleep(100);
            continue;
        }
        param->len = theclass->writebuf_list_.read(param->data,BUFFER_SIZE);
        uv_mutex_unlock(&theclass->mutex_writebuf_);

        uv_async_send(&param->async);
        uv_sem_wait(&param->semt);//Ҫֹͣ�̱߳����ȵ���uv_sem_post
    }
    free(param->data);
}

//�첽���÷���uv_write
void TCPClient::AsyncSendCB( uv_async_t* handle )
{
    TCPClient *theclass =(TCPClient*)handle->data;
    //uv_sem_wait(&theclass->sendparam_.semt);
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
    uv_buf_t buf = uv_buf_init(theclass->sendparam_.data,theclass->sendparam_.len);
    int iret = uv_write((uv_write_t*)req, (uv_stream_t*)&theclass->client_handle_, &buf, 1, AfterSend);
    if (iret) {
        uv_mutex_lock(&theclass->mutex_writereq_);
        theclass->writereq_list_.push_back(req);//����ʧ�ܣ��������AfterSend���������Եû���req
        uv_mutex_unlock(&theclass->mutex_writereq_);
        LOGE("�ͻ���("<<theclass<<") send error:"<<GetUVError(iret));
        fprintf(stdout,"send error. %s-%s\n",uv_err_name(iret),uv_strerror(iret));
    }
    uv_sem_post(&theclass->sendparam_.semt);
}

//�ͻ���-�������ݻص�����
void TCPClient::SetRecvCB(client_recvcb pfun, void* userdata )
{
    recvcb_ = pfun;
    recvcb_userdata_ = userdata;
}

void TCPClient::SetClosedCB( tcp_closed pfun, void* userdata )
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
    TCPClient *client = (TCPClient*)handle->data;
    *buf = client->readbuffer_;
}


void TCPClient::AfterRecv(uv_stream_t *handle, ssize_t nread, const uv_buf_t* buf)
{
    if (!handle->data) {
        return;
    }
    TCPClient *client = (TCPClient*)handle->data;//��������recv������TCPClient
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
        client->Close();
        return;
    }
    if (nread > 0) {
        //client->recvcb_(buf->base,nread,client->userdata_);//�ɷ�ʽ-�ص�������
        client->readpacket_.recvdata((const unsigned char*)buf->base,nread);//�·�ʽ-����������ٻص�����
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
    TCPClient *cdata = (TCPClient*)handle->data;
    fprintf(stdout,"Close CB handle %p\n",handle);
    if (handle == (uv_handle_t *)&cdata->sendparam_.async) {
        handle->data = 0;//��ֵ0�������ж��Ƿ���ù�
    }
    if (handle == (uv_handle_t *)&cdata->client_handle_) {
        handle->data = 0;//��ֵ0�������ж��Ƿ���ù�
    }
    if (cdata->sendparam_.async.data == 0 && cdata->client_handle_.data == 0) {
        cdata->isinit_ = false;
        LOGI("client  had closed.");
        if (cdata->closedcb_) {//֪ͨTCPServer�˿ͻ����Ѿ��ر�
            cdata->closedcb_(-1,cdata->closedcb_userdata_);
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


/*****************************************AcceptClient*************************************************************/
AcceptClient::AcceptClient( int clientid, char packhead, char packtail, uv_loop_t* loop /*= uv_default_loop()*/ )
    :client_id_(clientid),loop_(loop)
    ,writebuf_list_(BUFFER_SIZE),isinit_(false),is_writethread_stop_(true)
    ,recvcb_(nullptr),recvcb_userdata_(nullptr),closedcb_(nullptr),closedcb_userdata_(nullptr)
{
    readbuffer_ = uv_buf_init((char*)malloc(BUFFER_SIZE), BUFFER_SIZE);
    Start(packhead,packtail);
}

AcceptClient::~AcceptClient()
{
    if (readbuffer_.base) {
        free(readbuffer_.base);
        readbuffer_.base = NULL;
        readbuffer_.len = 0;
    }
}

bool AcceptClient::Start( char packhead, char packtail )
{
    if (isinit_) {
        return true;
    }
    if (!loop_) {
        errmsg_ = "loop is null on tcp init.";
        LOGE(errmsg_);
        return false;
    }
    //��ʼ��TCP handle
    int iret = uv_tcp_init(loop_,&client_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    client_handle_.data = this;

    //��ʼ��async �ص�
    iret = uv_async_init(loop_,&sendparam_.async,AsyncSendCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    sendparam_.async.data = this;
    fprintf(stdout,"client id %d, async handle %p\n",client_id_,&sendparam_.async);
    iret = uv_sem_init(&sendparam_.semt,0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    //uv_sem_post(&sendparam_.semt);

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
    is_writethread_stop_ = false;
    iret = uv_thread_create(&writethread_handle_,WriteThread,this);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }

    //����read��װ��
    readpacket_.SetPacketCB(GetPacket,this);
    readpacket_.Start(packhead,packtail);

    isinit_ = true;
    return true;
}

void AcceptClient::Stop()
{
    if (!isinit_) {
        return;
    }
    //ֹͣread��װ��
    readpacket_.Stop();

    is_writethread_stop_ = true;
    uv_sem_post(&sendparam_.semt);//�����źţ���writethread�߳��л����˳�
    int iret = uv_thread_join(&writethread_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        fprintf(stdout,"thread join error, %s-%s\n",uv_err_name(iret),uv_strerror(iret));
    }
    uv_sem_destroy(&sendparam_.semt);
    uv_mutex_destroy(&mutex_writebuf_);

    uv_mutex_lock(&mutex_writereq_);
    for (auto it= writereq_list_.begin(); it!=writereq_list_.end(); ++it) {
        free(*it);
    }
    writereq_list_.clear();
    uv_mutex_unlock(&mutex_writereq_);
    uv_mutex_destroy(&mutex_writereq_);

    uv_close((uv_handle_t*)&sendparam_.async,AfterClientClose);
    uv_close((uv_handle_t*)&client_handle_,AfterClientClose);
    //closedcb_����ʱ����ɾ������
}

//д�����߳�
void AcceptClient::WriteThread( void *arg )
{
    AcceptClient *theclass =(AcceptClient*)arg;
    sendparam *param = &theclass->sendparam_;
    param->data =(char*)malloc(BUFFER_SIZE);
    param->len = BUFFER_SIZE;
    while(!theclass->is_writethread_stop_) {
        uv_mutex_lock(&theclass->mutex_writebuf_);
        if(theclass->writebuf_list_.empty()) {
            uv_mutex_unlock(&theclass->mutex_writebuf_);
            ThreadSleep(100);
            continue;
        }
        param->len = theclass->writebuf_list_.read(param->data,BUFFER_SIZE);
        uv_mutex_unlock(&theclass->mutex_writebuf_);

        uv_async_send(&param->async);
        uv_sem_wait(&param->semt);//Ҫֹͣ�̱߳����ȵ���uv_sem_post
    }
    free(param->data);
}

//�ص�һ֡���ݸ��û�
void AcceptClient::GetPacket( const NetPacket& packethead, const unsigned char* packetdata, void* userdata )
{
    if (!userdata) {
        return;
    }
    AcceptClient *theclass = (AcceptClient*)userdata;
    if (theclass->recvcb_) {//�ѵõ������ݻص����û�
        theclass->recvcb_(theclass->client_id_,packethead,packetdata,userdata);
    }
}

//�������ݣ�������ѹ����У���WriteThread�̸߳����ͣ������޷���ֵ
int AcceptClient::Send( const char* data, std::size_t len )
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

void AcceptClient::AfterSend( uv_write_t *req, int status )
{
    //����uv_write_t
    AcceptClient *theclass = (AcceptClient*)req->data;
    uv_mutex_lock(&theclass->mutex_writereq_);
    theclass->writereq_list_.push_back(req);
    uv_mutex_unlock(&theclass->mutex_writereq_);
    if (status < 0) {
        LOGE("������������:"<<GetUVError(status));
        fprintf(stderr, "Write error %s.%s\n",uv_err_name(status),uv_strerror(status));
    }
}

//�첽���÷���uv_write
void AcceptClient::AsyncSendCB( uv_async_t* handle )
{
    AcceptClient *theclass =(AcceptClient*)handle->data;
    //uv_sem_wait(&theclass->sendparam_.semt);
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
    uv_buf_t buf = uv_buf_init(theclass->sendparam_.data,theclass->sendparam_.len);
    int iret = uv_write((uv_write_t*)req, (uv_stream_t*)&theclass->client_handle_, &buf, 1, AfterSend);
    if (iret) {
        uv_mutex_lock(&theclass->mutex_writereq_);
        theclass->writereq_list_.push_back(req);//����ʧ�ܣ��������AfterSend���������Եû���req
        uv_mutex_unlock(&theclass->mutex_writereq_);
        LOGE("�ͻ���("<<theclass<<") send error:"<<GetUVError(iret));
        fprintf(stdout,"send error. %s-%s\n",uv_err_name(iret),uv_strerror(iret));
    }
    uv_sem_post(&theclass->sendparam_.semt);
}

bool AcceptClient::AcceptByServer( uv_tcp_t* server )
{
    int iret = uv_accept((uv_stream_t*)server, (uv_stream_t*)&client_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_read_start((uv_stream_t*)&client_handle_, AllocBufferForRecv, AfterRecv);//��������ʼ���տͻ��˵�����
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

//����ռ��������
void AcceptClient::AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    if (!handle->data) {
        return;
    }
    AcceptClient *client = (AcceptClient*)handle->data;
    *buf = client->readbuffer_;
}
//�������ݻص�
void AcceptClient::AfterRecv(uv_stream_t *handle, ssize_t nread, const uv_buf_t* buf)
{
    if (!handle->data) {
        return;
    }
    AcceptClient *client = (AcceptClient*)handle->data;//��������recv������clientdata
    if (nread < 0) {/* Error or EOF */
        if (nread == UV_EOF) {
            fprintf(stdout,"�ͻ���(%d)�����Ͽ�\n",client->client_id_);
            LOGW("�ͻ���("<<client->client_id_<<")�����Ͽ�");
        } else if (nread == UV_ECONNRESET) {
            fprintf(stdout,"�ͻ���(%d)�쳣�Ͽ�\n",client->client_id_);
            LOGW("�ͻ���("<<client->client_id_<<")�쳣�Ͽ�");
        } else {
            fprintf(stdout,"%s\n",GetUVError(nread));
            LOGW("�ͻ���("<<client->client_id_<<")�쳣�Ͽ���"<<GetUVError(nread));
        }
        client->Stop();
        return;
    } else if (0 == nread)  {/* Everything OK, but nothing read. */

    } else {
        client->readpacket_.recvdata((const unsigned char*)buf->base,nread);//�·�ʽ-����������ٻص�����
    }
}

void AcceptClient::AfterClientClose( uv_handle_t *handle )
{
    AcceptClient *cdata = (AcceptClient*)handle->data;
    if (handle == (uv_handle_t *)&cdata->sendparam_.async) {
        handle->data = 0;//��ֵ0�������ж��Ƿ���ù�
    }
    if (handle == (uv_handle_t *)&cdata->client_handle_) {
        handle->data = 0;//��ֵ0�������ж��Ƿ���ù�
    }
    if (cdata->sendparam_.async.data == 0 && cdata->client_handle_.data == 0) {
        cdata->isinit_ = false;
        LOGI("client "<<cdata->client_id_<<" had closed.");
        if (cdata->closedcb_) {//֪ͨTCPServer�˿ͻ����Ѿ��ر�
            cdata->closedcb_(cdata->client_id_,cdata->closedcb_userdata_);
        }
    }
}

void AcceptClient::SetRecvCB( server_recvcb pfun, void* userdata )
{
    //��GetPacket����
    recvcb_ = pfun;
    recvcb_userdata_ = userdata;
}

void AcceptClient::SetClosedCB( tcp_closed pfun, void* userdata )
{
    //��AfterRecv����
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}

/*****************************************Common Fun*************************************************************/
std::string PacketData( NetPacket& packet, const unsigned char* data )
{
    unsigned char md5str[MD5_DIGEST_LENGTH];
    MD5_CTX md5;
    MD5_Init(&md5);
    MD5_Update(&md5,data,packet.datalen);
    MD5_Final(md5str,&md5);
    memcpy(packet.check,md5str,MD5_DIGEST_LENGTH);

    unsigned char packchar[NET_PACKAGE_HEADLEN];
    NetPacketToChar(packet,packchar);

    std::string retstr;
    retstr.append(1,packet.header);
    retstr.append((const char*)packchar,NET_PACKAGE_HEADLEN);
    retstr.append((const char*)data,packet.datalen);
    retstr.append(1,packet.tail);
    return std::move(retstr);
}

}