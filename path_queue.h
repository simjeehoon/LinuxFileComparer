#ifndef __PATH_QUEUE_H__
#define __PATH_QUEUE_H__

#include <stddef.h>

//PathQueue 구현을 위한 링크드 리스트 노드 구조체
typedef struct _linked_list_node{
	char *data;
	struct _linked_list_node *prev;
	struct _linked_list_node *next;
}LinkedListNode;

//PathQueue를 위한 링크드 리스트 노드 생성 함수
LinkedListNode *make_linked_list_node(const char *, LinkedListNode *prev, LinkedListNode *next);
//PathQueue에 쓰이는 링크드 리스트 노드를 삭제하는 함수
void delete_linked_list_node(LinkedListNode *node);

//Path가 담길 큐 구조체
typedef struct _path_queue{
	LinkedListNode* head;
	LinkedListNode* tail;
	int cnt;
}PathQueue;

//Path 큐 생성 함수
PathQueue *make_path_queue();
//Path 큐 삭제 함수
void delete_path_queue(PathQueue *queue);

//큐가 비어있는지 여부 리턴
//비어있으면 0이 아닌 값, 비어있지 않으면 0 리턴
int is_queue_empty(const PathQueue *queue);

//큐에 path 삽입
//queue의 원소 개수 리턴
int enqueue(PathQueue *queue, const char *path);

//큐에 있는 path를 빼냄.
//정상적이면 path 리턴, 큐가 비어있으면 NULL 리턴
char *dequeue(PathQueue *queue, char *path);

#endif
