/*
* ��С�ѣ�
�����ж�ʱ���г�ʱʱ����С��һ����ʱ���ĳ�ʱֵ��Ϊ�Ĳ������
������һ���Ĳ�����tick�����ã���ʱʱ����С�Ķ�ʱ����Ȼ���ڣ�
���ǾͿ�����tick�����д���ö�ʱ����
Ȼ���ٴδ�ʣ��Ķ�ʱ�����ҳ���ʱʱ����С��һ����
���������Сʱ������Ϊ��һ���Ĳ������
��˷�������ʵ���˽�Ϊ��ȷ�Ķ�ʱ��
*/

/*
* ���˲���
   ���ýڵ㲻�������ƶ���ֱ������
* ���˲���
   ����ǰ�ڵ㲻�������ƶ���ֱ������
*/

#ifndef MIN_HEAP_H
#define MIN_HEAP_H

#include <iostream>
#include <netinet/in.h>
#include <ctime>

using std::exception;

//#define BUFFER_SIZE 64

//class util_timer;
class heap_timer;  //ǰ������

//struct client_data
//{
//    sockaddr_in address;
//    int sockfd;
//    util_timer* timer;
//};
struct client_data {
    sockaddr_in address;
    int sockfd;
    //char buf[BUFFER_SIZE];
    heap_timer* timer;
};

//class util_timer
//{
//public:
//    util_timer() : prev(NULL), next(NULL) {}
//
//public:
//    time_t expire;//��ʱʱ��
//    void (*cb_func)(client_data*);//��ʱ���ص�����
//    client_data* user_data;//�ͻ�������
//    util_timer* prev;//�ϸ��ڵ�
//    util_timer* next;//�¸��ڵ�
//};

class heap_timer {
public:
    //heap_timer(int delay) {
    //    expire = time(NULL) + delay;
    //}
public:
    time_t expire;  //��ʱ����Ч�ľ���ʱ��
    void (*cb_func)(client_data*); // ��ʱ���Ļص�����
    client_data* user_data; //�û�����
    void setIdx(int idx) {
        this->idx = idx;
    }
    int getIdx() {
        return this->idx;
    }
private:
    int idx = -1;//����������е�λ��
};

//class sort_timer_lst
//{
//public:
//    sort_timer_lst();
//    ~sort_timer_lst();
//
//    void add_timer(util_timer* timer);
//    void adjust_timer(util_timer* timer);
//    void del_timer(util_timer* timer);
//    void tick();//��������
//
//private:
//    void add_timer(util_timer* timer, util_timer* lst_head);
//
//    util_timer* head;
//    util_timer* tail;
//};
class time_heap {
public:
    time_heap(int cap = 128) throw(std::exception);//����cap��С�Ķѣ�ʹ������ģ������
    time_heap(heap_timer** init_array, int size, int capacity) throw(std::exception);//ʹ�������ʼ����
    ~time_heap();

public:
    //�����и���ʱ��ڵ㺯�����Ѹ���δ���
    void add_timer(heap_timer* timer) throw(std::exception);//���Ӷ�ʱ��
    void adjust_timer(heap_timer* timer);//���¶�ʱ��
    void del_timer(heap_timer* timer);//ɾ����ʱ��
    void pop_timer();
    void tick();//��������
    heap_timer* top() const {
        if (empty())
            return NULL;
        return array[0];
    }
    bool empty() const {
        return cur_size == 0;
    }

private:
    //��С�ѵ����ǲ�����ȷ�����������Ե�hole���ڵ���Ϊ��������ӵ����С������
    void percolate_down(int hole) {
        heap_timer* tmp = array[hole];
        int child = 0;
        for (; ((hole * 2 + 1) <= (cur_size - 1)); hole = child) {
            child = hole * 2 + 1;
            if ((child < cur_size - 1) && (array[child + 1]->expire < array[child]->expire)) {
                child++;
            }
            if (array[child]->expire < tmp->expire) {  // ���ӵ�Ȩֵ���Ȳ����С��ֻ���������ˡ�
                array[hole] = array[child];
                array[hole]->setIdx(hole);
            }
            else
                break;
        }
        array[hole] = tmp;
        tmp->setIdx(hole);
    }
    void resize() throw(std::exception) {

        heap_timer** tmp = new heap_timer * [capacity * 2];
        for (int j = 0; j < capacity * 2; ++j) {
            tmp[j] = NULL;
        }
        if (!tmp) {
            throw std::exception();
        }
        for (int i = 0; i < cur_size; i++) {
            tmp[i] = array[i];
        }
        delete[]array;//����ԭ��������
        capacity = capacity * 2;
        array = tmp;
    }
private:
    heap_timer** array; //������
    int capacity; //�����������
    int cur_size; //���������Ԫ�صĸ���
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //���ļ����������÷�����
    int setnonblocking(int fd);

    //���ں��¼���ע����¼���ETģʽ��ѡ����EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //�źŴ�����
    static void sig_handler(int sig);

    //�����źź���
    void addsig(int sig, void(handler)(int), bool restart = true);

    //��ʱ�����������¶�ʱ�Բ��ϴ���SIGALRM�ź�
    void timer_handler();

    void show_error(int connfd, const char* info);

public:
    static int* u_pipefd;
    time_heap m_time_heap;
    static int u_epollfd;
    int m_TIMESLOT = -1;//��ʱʱ��
};

void cb_func(client_data* user_data);

#endif //MIN_HEAP_H