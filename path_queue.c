#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "path_queue.h"

//PathQueue를 위한 링크드 리스트 노드 생성 함수
LinkedListNode *make_linked_list_node(const char *data, LinkedListNode *prev, LinkedListNode *next){
	/*노드 동적 할당*/
	LinkedListNode * node = (LinkedListNode *)malloc(sizeof(LinkedListNode));
	/*노드 변수 초기화*/
	node->prev = prev;
	node->next = next;
	if(data != NULL){ //path 스트링의 포인터 data의 내용을 복사.
		node->data = (char *)malloc((strlen(data)+1)*sizeof(char));
		strcpy(node->data, data);
	} else {
		node->data = NULL;
	}
	return node;
}

//PathQueue에 쓰이는 링크드 리스트 노드를 삭제하는 함수
void delete_linked_list_node(LinkedListNode *node){
	/*path data가 동적할당이 되어있다면*/
	if(node->data != NULL){
		free(node->data);
	}
	free(node);
}

//Path 큐 생성 함수
PathQueue *make_path_queue(){
	/*노드 동적 할당*/
	PathQueue *queue = (PathQueue *)malloc(sizeof(PathQueue));
	/*head와 tail에 더미 노드 생성*/
	queue->head = make_linked_list_node(NULL, NULL, NULL);
	queue->tail = make_linked_list_node(NULL, NULL, NULL);
	/*더미 노드의 링크 설정*/
	queue->head->prev = queue->head;
	queue->head->next = queue->tail;
	queue->tail->prev = queue->head;
	queue->tail->next = queue->tail;
	/*노드 개수 설정*/
	queue->cnt = 0; 
	return queue;
};

//Path 큐 삭제 함수
void delete_path_queue(PathQueue *queue){
	LinkedListNode* cur = queue->head;
	LinkedListNode* tmp = NULL;
	while(cur != queue->tail){ //tail이 아닐때까지
		tmp = cur->next; //다음 링크 임시 저장
		delete_linked_list_node(cur);
		cur = tmp;
	}
	delete_linked_list_node(queue->tail);
	free(queue);
};

//큐가 비어있는지 여부 리턴
//비어있으면 0이 아닌 값, 비어있지 않으면 0 리턴
int is_queue_empty(const PathQueue *queue){
	return queue->cnt == 0;
}

//큐에 path 삽입
//queue의 원소 개수 리턴
int enqueue(PathQueue *queue, const char *path){
	LinkedListNode *newNode = make_linked_list_node(path, queue->tail->prev, queue->tail);
	/*링크드 리스트의 맨 뒤에 삽입*/
	queue->tail->prev->next = newNode;
	queue->tail->prev = newNode;
	return ++(queue->cnt);
};

//큐에 있는 path를 빼냄.
//정상적이면 path 리턴, 큐가 비어있으면 NULL 리턴
char *dequeue(PathQueue *queue, char *path){
	if(queue->cnt > 0){
		strcpy(path, queue->head->next->data);//맨 앞의 노드에 있는 path를 인자 path에 복사
		LinkedListNode *todel = queue->head->next; //맨 앞의 노드를 todel에 할당
		todel->next->prev = queue->head; //지울 노드의 next 노드의 prev 링크 재설정
		queue->head->next = todel->next; //헤드 노드의 next 노드 재설정
		delete_linked_list_node(todel); //todel 노드 삭제
		--(queue->cnt); 
		return path;//스트링을 복사한 포인터 리턴
	}
	else {// queue가 비어있으면 NULL 리턴
		return NULL;
	}
};

