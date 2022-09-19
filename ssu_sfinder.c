#include "path_queue.h"

#define MAX_THREAD 5
#define SHA_BUFSIZE 1024*16
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <pthread.h>
#include <pwd.h>

#define HASH_SIZE SHA_DIGEST_LENGTH

#define LOG_FILE_NAME ".duplicate_20172644.log"

//pthread 관련 전역 변수
static pthread_mutex_t fileset_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

//로그 파일
FILE *__logfile;
char *trash_bin_files_path; //휴지통 파일 디렉토리 경로
char *trash_bin_info_path; //휴지통 정보 디렉토리 경로
char *user_name; //사용자 이름

/*휴지통 파일 정보*/
typedef struct _trash_info{
	char real_path[4097];//원래 있던 전체경로
	char alias[256];//휴지통 안에서의 이름
	char date[9];//삭제한 날짜 8자
	char time[7];//삭제한 시간 6자
	long long size;//파일 크기
	unsigned char hash[HASH_SIZE];//파일 해쉬
}TrashInfo;

/*휴지통 파일 링크드리스트 노드*/
typedef struct _trash_node{
	TrashInfo *data;
	struct _trash_node *prev;
	struct _trash_node *next;
}TrashNode;

/*휴지통 파일 링크드리스트*/
typedef struct trash_linked_list{
	TrashNode *head;
	TrashNode *tail;
	int node_cnt;
	char *trash_info_file_path;
} TrashLinkedList; 

//휴지통 링크드리스트를 전역으로 선언
TrashLinkedList global_trash_list;
int save_trash_list();

//TrashInfo를 동적할당하여 리턴
TrashInfo *make_trashinfo(const char* real_path, const char* alias, time_t *deletion_time, long long size, const unsigned char *hash){
	TrashInfo *data = (TrashInfo *)malloc(sizeof(TrashInfo));
	strcpy(data->real_path, real_path);
	strcpy(data->alias, alias);
	struct tm *tm = localtime(deletion_time);
	sprintf(data->date, "%04d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
	sprintf(data->time, "%02d%02d%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
	data->size = size;
	if(hash != NULL){
		memcpy(data->hash, hash, HASH_SIZE);
	}
	else{
		memset(data->hash, 0, HASH_SIZE);
	}
	return data;
}

//global_trash_list의 맨 뒤에 TrashInfo 삽입
//인자로 들어온 data는 동적 할당된 메모리여야함
//해당 메모리를 그대로 사용
int add_trash_info(TrashInfo* data, int (*savefunc)()){
	TrashNode *newnode = (TrashNode *) malloc(sizeof(TrashNode));
	newnode->data = data;
	newnode->prev = global_trash_list.tail->prev;
	newnode->next = global_trash_list.tail;
	global_trash_list.tail->prev->next = newnode;
	global_trash_list.tail->prev = newnode;
	global_trash_list.node_cnt++;
	if(savefunc == NULL)
		return 0;
	else{
		return savefunc();
	}
}

//global_trash_list의 index번째 TrashNode 포인터 리턴
TrashNode *get_trashnode_with_index(int index){
	if(global_trash_list.node_cnt < index || index <= 0){
		return NULL;
	}
	TrashNode *dst = global_trash_list.head;
	for(int i = 0 ; i < index ; i++){
		dst = dst->next;
	}
	return dst;
}

//인자로 들어온 TrashNode를 global_trash_list에서 삭제
//savefunc 함수가 있다면 savefunc를 수행
TrashNode *delete_trashnode(TrashNode* trashnode, int (*savefunc)()){
	if(trashnode == global_trash_list.head || trashnode == global_trash_list.tail)
		return NULL;
	TrashNode *returnval = trashnode->next;	
	trashnode->prev->next = trashnode->next;
	trashnode->next->prev = trashnode->prev;
	global_trash_list.node_cnt--;
	if(trashnode->data != NULL){
		free(trashnode->data);
	}
	free(trashnode);
	if(savefunc != NULL)
		savefunc();
	return returnval;
}

//전역 휴지통 리스트 초기화
void init_trash_list(char *trash_info_file_path){
	//trashinfo 파일 경로 저장
	global_trash_list.trash_info_file_path = trash_info_file_path;
	//더미노드 생성
	global_trash_list.head = (TrashNode*) malloc(sizeof(TrashNode));
	global_trash_list.tail = (TrashNode*) malloc(sizeof(TrashNode));
	global_trash_list.head->data = NULL;
	global_trash_list.tail->data = NULL;

	//더미노드 연결 설정
	global_trash_list.head->prev = global_trash_list.head;
	global_trash_list.tail->next = global_trash_list.tail;
	global_trash_list.head->next = global_trash_list.tail;
	global_trash_list.tail->prev = global_trash_list.head;
	//노드 개수 초기화
	global_trash_list.node_cnt = 0;
	
	if(access(trash_info_file_path, F_OK) == 0){//trashinfo 파일이 존재
		int fd = open(global_trash_list.trash_info_file_path, O_RDONLY | O_LARGEFILE);
		if(fd < 0){
			puts("error");
			return;
		}
		int node_cnt;
		read(fd, &node_cnt, sizeof(node_cnt));//노드 개수 읽기
		TrashInfo tbuf;
		for(int i = 0 ; i < node_cnt ; i++){ //노드 개수만큼 데이터 읽기
			read(fd, &tbuf, sizeof(TrashInfo));
			TrashInfo *allocdata = (TrashInfo *)malloc(sizeof(TrashInfo));
			memcpy(allocdata, &tbuf, sizeof(TrashInfo));
			add_trash_info(allocdata, NULL);//읽은 데이터는 링크드리스트에 추가
		}
		close(fd);
	}
	else{
		//trashinfo 파일 생성
		save_trash_list();
	}
}

//메모리상의 전역 휴지통 리스트를 파일로 저장
int save_trash_list(){
	//trashinfo 파일 쓰기 전용으로 열기
	int fd = open(global_trash_list.trash_info_file_path, O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, 0644);
	if(fd < 0){
		fprintf(stderr, "save trash info error.\n");
		return -1;
	}
	//노드 개수 쓰기
	write(fd, &global_trash_list.node_cnt, sizeof(global_trash_list.node_cnt));
	TrashNode *cur = global_trash_list.head->next;
	while(cur != global_trash_list.tail){ //순차적으로 trashinfo 저장
		write(fd, cur->data, sizeof(TrashInfo));
		cur = cur->next;
	}
	close(fd);
	return 0;
}

//date 혹은 time 데이터를 형식에 맞게 출력
//delim이 '-'이면 date 출력
//delim이 ':'이면 time 출력
//width만큼 여백 생성
void __timefp(const char* str, char delim, int width){
	int i = 0;
	if(delim == '-'){
		for(; i < 4 ; i++) putchar(str[i]);
		putchar(delim);
		for(; i < 6 ; i++) putchar(str[i]);
		putchar(delim);
		for(; i < 8 ; i++) putchar(str[i]);
	}
	else if(delim == ':'){
		for(; i < 2 ; i++) putchar(str[i]);
		putchar(delim);
		for(; i < 4 ; i++) putchar(str[i]);
		putchar(delim);
		for(; i < 6 ; i++) putchar(str[i]);
	}
	for(; i < width-2 ; i++) putchar(' ');
}

//trash_list를 출력
int print_trash_list(){
	if(global_trash_list.node_cnt == 0){
		printf("Trash bin is empty\n");
		return 0;
	}
	else{
		printf("%s %-60s %-20s %-20s %-20s\n", "      ",
				"FILENAME", "SIZE", "DELETION DATE", "DELETION TIME");
		TrashNode *cur = global_trash_list.head->next;
		for(int i = 1 ; i <= global_trash_list.node_cnt ; i++){
			TrashInfo *data = cur->data;
			printf("[%4d] %-60s %-20lld ", i, data->real_path, data->size);
			__timefp(data->date, '-', 20);
			putchar(' ');
			__timefp(data->time, ':', 0);
			putchar('\n');
			cur = cur->next;
		}
		return global_trash_list.node_cnt;
	}
}

//삽입 정렬을 이용하여 휴지통리스트 정렬
void trash_list_sort(int (*cmp)(const TrashInfo*, const TrashInfo*, bool), bool dec){
	TrashNode *sorted_end = global_trash_list.head->next->next, *sorted_cur;
	TrashInfo *key;

	while(sorted_end != global_trash_list.tail){
		key = sorted_end->data;
		sorted_cur = sorted_end->prev;
		while(sorted_cur != global_trash_list.head){
			if(cmp(sorted_cur->data, key, dec) > 0){
				sorted_cur->next->data = sorted_cur->data;
				sorted_cur = sorted_cur->prev;
			}
			else
				break;
		}
		sorted_cur->next->data = key;
		sorted_end = sorted_end->next;
	}
}

//trash_list_sort의 인자로 넘길 비교 함수들
int tl_default_cmp(const TrashInfo *p1, const TrashInfo *p2, bool sort_decreasing){
	char *name1 = strrchr(p1->real_path, '/')+1;
	char *name2 = strrchr(p2->real_path, '/')+1;
	if(name1 == NULL && name2 == NULL) return 0;
	else if(name2==NULL) return sort_decreasing ? -1 : 1;
	else if(name1==NULL) return sort_decreasing ? 1 : -1;
	else{
		if(sort_decreasing)
			return strcmp(name1, name2) * -1;
		else
			return strcmp(name1, name2);
	}
}
int tl_filename_cmp(const TrashInfo *p1, const TrashInfo *p2, bool sort_decreasing){
	if(sort_decreasing)
		return strcmp(p1->real_path, p2->real_path) * -1;
	else
		return strcmp(p1->real_path, p2->real_path);
}
int tl_size_cmp(const TrashInfo *p1, const TrashInfo *p2, bool sort_decreasing){
	if(p1->size < p2->size)
		return sort_decreasing ? 1 : -1;
	else if(p1->size > p2->size)
		return sort_decreasing ? -1 : 1;
	else
		return 0;
}
int tl_date_cmp(const TrashInfo *p1, const TrashInfo *p2, bool sort_decreasing){
	if(sort_decreasing)
		return strcmp(p1->date, p2->date) * -1;
	else
		return strcmp(p1->date, p2->date);
}
int tl_time_cmp(const TrashInfo *p1, const TrashInfo *p2, bool sort_decreasing){
	if(sort_decreasing)
		return strcmp(p1->time, p2->time) * -1;
	else
		return strcmp(p1->time, p2->time);
}

#define RM "REMOVE"
#define DEL "DELETE"
#define RST "RESTORE"
//로그를 기록하는 함수
void wlog(const char *command, const char *path, time_t *time){
	char timestrbuf[40];
	strftime(timestrbuf, sizeof(timestrbuf), "%Y-%m-%d %X", localtime(time));
	fprintf(__logfile, "[%s] %s %s %s\n", command, path, timestrbuf, user_name);
	fflush(__logfile);
}

//long long 타입의 num을 3자리마다 ','를 삽입하여 출력
void print_lld_with_comma(long long num){
	char buf[120] ;
	sprintf(buf, "%lld", num);
	int j, len;
	j = len = strlen(buf);
	for(int i = 0 ; i < len ; i++){
		putchar(buf[i]);
		if(--j % 3 == 0 && j > 0)//comma 삽입
			putchar(',');
	}
}

/* 해쉬를 구하는 함수
 * dst 배열의 사이즈는 HASH_SIZE여야 합니다. */
int get_hash(const char *path, unsigned char *dst){
    int bytes;
    unsigned char data[SHA_BUFSIZE] = {0};
    SHA_CTX shaContext;
    SHA1_Init(&shaContext);
	int fd = open(path, O_RDONLY | O_LARGEFILE);
	if(fd < 0){
		printf("open failed.");
		return -1;
	}
    while (1){
		bytes = read(fd, data, sizeof(data));
		if(bytes < 0){
			printf("ERROR while hashing(sha1)\n");
			return -1;
		}
		else if(bytes == 0)
			break;
		else
			SHA1_Update(&shaContext, data, bytes);
	}
    SHA1_Final(dst,&shaContext);
	close(fd);
    return 0;
}

//hash를 화면에 출력 
void print_hash(const unsigned char *hash){
	for(size_t i = 0 ; i< HASH_SIZE ; i++){
		printf("%02x", hash[i]);
	}
}

//time_t를 포맷에 맞게 출력하는 함수
void print_time(time_t src_time){
	struct tm* timeinfo = localtime(&src_time);
	printf("%04d-%02d-%02d %02d:%02d:%02d",
			timeinfo->tm_year+1900, timeinfo->tm_mon+1, timeinfo->tm_mday,
			timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

//hash1과 hash2를 비교
//같은 hash이면 true, 아니면 false 리턴
bool is_equal_hash(const unsigned char* hash1, const unsigned char* hash2){
	for(size_t i = 0 ; i < HASH_SIZE; i++)
		if(hash1[i] != hash2[i])
			return false;
	return true;
}

//검색된 개별 파일 정보 구조체
typedef struct {
	char * full_path;
	time_t mtime;
	time_t atime;
	uid_t uid;
	gid_t gid;
	mode_t mode;
}EachFile;

// EachFile을 담는 양방향 링크드 리스트 노드
typedef struct _e_node{
	EachFile *data;
	struct _e_node *prev, *next;
}EachFileNode;

// EachFile에 대한 정보의 링크드리스트, 해쉬값, 파일 크기를 담는 구조체
typedef struct{
	long long file_size;
	unsigned char *hash;
	int file_count;
	EachFileNode *head, *tail;
}FileList;

//FileList를 담는 양방향 링크드 리스트 노드
typedef struct _fl_node{
	FileList *data;
	struct _fl_node *prev, *next;
}FileListNode;

//FileList의 링크드리스트 구조체
typedef struct FileSet{
	int node_cnt;
	FileListNode *head, *tail;
} FileSet;

//FileSet은 전역으로 선언되며, fsha1이 실행되지 않았다면 node_cnt에는 1이 할당
FileSet global_fileset = {-1, NULL, NULL};

//FileSet에 있는 FileList중 원소 개수가 2개 이상인 list의 개수 리턴
int get_duplicate_cnt(){
	if(global_fileset.node_cnt < 0) //FileSet이 초기화가 안되어도 0 리턴
		return 0;
	FileListNode *target = global_fileset.head->next;
	int i = 0;
	while(target != global_fileset.tail){
		if(target->data->file_count >= 2){
			++i;
		}
		target = target->next;
	}
	return i;
}

/* EachFileNode를 만드는 함수.
 * 파라미터 data는 동적 할당된 메모리여야함.
 */
EachFileNode *make_each_file_node(EachFile *data, 
		EachFileNode *prev, EachFileNode *next){
	/*EachFileNode를 위한 동적할당*/
	EachFileNode *node = (EachFileNode*)malloc(sizeof(EachFileNode));
	/*EachFileNode 초기화*/
	node->prev = prev;
	node->next = next;
	node->data = data; 
	return node;
}

/* EachFileNode를 제거하는 함수.
 * data가 동적할당되었으면 동적 해제하고 노드 삭제.
 * return 값은 node->next
 */
EachFileNode *delete_each_file_node(EachFileNode *node){
	EachFileNode *tmp = node->next;
	node->prev->next = node->next;
	node->next->prev = node->prev;
	if(node->data != NULL){
		if(node->data->full_path != NULL)
			free(node->data->full_path);
		free(node->data);
	}
	free(node);
	return tmp;
}

//FileList를 생성하는 함수
//hash는 동적할당되어야 하며 동적할당 메모리를 그대로 이용한다.
FileList *make_filelist(long long file_size, unsigned char* hash){
	/*file_list를 위한 동적할당*/
	FileList *file_list = (FileList*)malloc(sizeof(FileList));
	/*file_list 초기화*/
	file_list->file_size = file_size; //file_size 배정 
	file_list->hash = hash;
	/*링크드 리스트 초기화*/
	file_list->file_count = 0;
	file_list->head = make_each_file_node(NULL, NULL, NULL);
	file_list->tail = make_each_file_node(NULL, NULL, NULL);
	file_list->head->prev = file_list->head;
	file_list->head->next = file_list->tail;
	file_list->tail->prev = file_list->head;
	file_list->tail->next = file_list->tail;
	return file_list;
}

//FileList를 동적 해제하는 함수
void delete_filelist(FileList *list){
	if(list->hash != NULL){
		free(list->hash); //hash  메모리 동적 해제 
	}
	EachFileNode *delnode = list->head->next;
	/*더미 노드를 제외한 EachFileNode삭제*/
	while(delnode != list->tail)
		delnode = delete_each_file_node(delnode);
	/*더미 노드 삭제*/
	free(list->head);
	free(list->tail);
}

/* FileListNode를 만드는 함수.
 * 파라미터 data는 동적 할당된 메모리여야함.
 */
FileListNode *make_filelist_node(FileList *data, 
		FileListNode *prev, FileListNode *next){
	/*FileListNode를 위한 동적할당*/
	FileListNode *node = (FileListNode*)malloc(sizeof(FileListNode));
	/*fs_node 초기화*/
	node->prev = prev;
	node->next = next;
	node->data = data; 
	return node;
}

/* FileListNode를 제거하는 함수.
 * data가 동적할당되었으면 동적 해제하고 노드 삭제.
 * return 값은 node->next
 */
FileListNode *delete_filelist_node(FileListNode *node){
	FileListNode *tmp = node->next;
	node->prev->next = node->next;
	node->next->prev = node->prev;
	if(node->data != NULL)
		delete_filelist(node->data);
	free(node);
	return tmp;
}

//FileSet 초기화 함수
void init_fileset(FileSet *fs){
	fs->node_cnt = 0; //노드 개수 초기화
	/*더미 노드 생성*/
	fs->head = make_filelist_node(NULL, NULL, NULL);
	fs->tail = make_filelist_node(NULL, NULL, NULL);
	fs->head->next = fs->tail;
	fs->head->prev = fs->head;
	fs->tail->next = fs->tail;
	fs->tail->prev = fs->head;
}

//FileSet 동적 해제 함수
void free_fileset(FileSet *fs){
	FileListNode *node = fs->head->next;
	while(node != fs->tail)
		node = delete_filelist_node(node);
	free(fs->head);
	free(fs->tail);
	fs->node_cnt = -1;
}

/* FileList의 가장 뒤에 file_data를 삽입하는 함수
 * 반드시 원자적으로 수행되어야 한다
 * 정렬이 보장되지 않는다.*/
int _push_back_to_filelist(FileList *filelist, EachFile *file_data){
	EachFileNode *new_node = make_each_file_node(file_data, filelist->tail->prev, filelist->tail);
	filelist->tail->prev->next = new_node;
	filelist->tail->prev = new_node;
	return ++(filelist->file_count);
}

/*fileset에 해쉬 정보를 삽입한다.
 * fileset 데이터 수정이 일어나므로
 * 반드시 원자적으로 수행되어야 한다.
 * file_data는 동적할당되어야 한다.
 */
int insert_to_glob_fileset(EachFile *file_data, long long file_size, unsigned char *hash){
	FileListNode *node = global_fileset.head->next;
	//file_size가 0일 경우 첫 노드에 삽입
	if(hash == NULL && file_size == 0){
		if(node != global_fileset.tail && node->data->file_size == 0)//이미 노드 존재
			return _push_back_to_filelist(node->data, file_data);
		else{
			FileListNode *newnode = make_filelist_node( //새 노드 생성
					make_filelist(0, NULL), //노드 내 data 생성
					node->prev, node);
			node->prev->next = newnode;
			node->prev = newnode;
			global_fileset.node_cnt++;
			return _push_back_to_filelist(newnode->data, file_data); //1 리턴
		}
	}
	//file_size가 일치하고 해쉬가 일치하는 노드 탐색
	else{
		for(; node != global_fileset.tail && node->data->file_size <= file_size
				; node = node->next){
			if(node->data->file_size == file_size){//사이즈 일치
				if(is_equal_hash(node->data->hash, hash)){//해쉬 일치
					free(hash);
					return _push_back_to_filelist(node->data, file_data); 
				}
			}
		}

		/*해쉬에 해당하는 노드를 못찾았다면
		 * 노드를 추가한다*/
		FileListNode *newnode = make_filelist_node( //새 노드 생성
				make_filelist(file_size, hash), //노드 내 data 생성
				node->prev, node);
		node->prev->next = newnode;
		node->prev = newnode;
		global_fileset.node_cnt++;
		return _push_back_to_filelist(newnode->data, file_data); //1 리턴
	}
}

//eachfile 구조체를 동적 생성한다.
EachFile* make_eachfile(const char *full_path, const struct stat64 *st){
	/*node 동적 할당*/
	EachFile *file_data = (EachFile*)malloc(sizeof(EachFile));
	/*nodedata 초기화*/
	file_data->atime = st->st_atime;
	file_data->mtime = st->st_mtime;
	file_data->uid = st->st_uid;
	file_data->gid = st->st_gid;
	file_data->mode = st->st_mode;
	file_data->full_path = (char *)malloc(strlen(full_path)+1);
	strcpy(file_data->full_path, full_path);
	return file_data;
}

//FileSet을 화면에 출력
void print_fileset(){
	if(get_duplicate_cnt() < 0){
		puts("there's no fileset.");
		return;
	}
	FileListNode *curFileList = global_fileset.head->next;
	EachFileNode *eachFileNode = NULL;
	int i = 0;
	while(curFileList != global_fileset.tail){
		if(curFileList->data->file_count >= 2){//노드 개수가 2개 이상인 것만 출력
			printf("---- identical files #%d (", ++i);
			print_lld_with_comma(curFileList->data->file_size);
			printf(" bytes");
			if(curFileList->data->file_size != 0){//file_size가 0이 아닐때만 hash 출력
				printf(" - ");
				print_hash(curFileList->data->hash);
			}
			printf(") ----\n");
			eachFileNode = curFileList->data->head->next; //처음 노드 할당
			for(int j = 0 ; j < curFileList->data->file_count ; j++){
				printf("[%d] %s (mtime : ", j+1, eachFileNode->data->full_path);
				print_time(eachFileNode->data->mtime);	//mtime 출력
				printf(") (atime : ");
				print_time(eachFileNode->data->atime);  //atime 출력
				printf(") (uid : %d) (gid : %d) (mode : %o)\n",
						eachFileNode->data->uid, eachFileNode->data->gid, eachFileNode->data->mode);
				//uid, gid, mode 출력
				eachFileNode = eachFileNode->next;
			}
			putchar('\n');
		}
		curFileList = curFileList->next;
	}
}

//file_list를 cmp에 맞춰 정렬시킨다.
void file_list_sort(FileList *flist, int (*cmp)(const EachFile*, const EachFile*, bool decreasing), bool decreasing){
	EachFileNode *sorted_end = flist->head->next->next, *sorted_cur;
	EachFile *key;

	while(sorted_end != flist->tail){
		key = sorted_end->data;
		sorted_cur = sorted_end->prev;
		while(sorted_cur != flist->head){
			if(cmp(sorted_cur->data, key, decreasing) > 0){
				sorted_cur->next->data = sorted_cur->data;
				sorted_cur = sorted_cur->prev;
			}
			else
				break;
		}
		sorted_cur->next->data = key;
		sorted_end = sorted_end->next;
	}
}

/*file_list를 정렬시키는데 사용되는 비교 함수들*/
int fl_filename_cmp(const EachFile*p1, const EachFile*p2, bool sort_decreasing){
	int p1depth = 0, p2depth =0;
	int i, j;
	for(i = 0 ; p1->full_path[i] != '\0' ; i++){
		if(p1->full_path[i] == '/')
			p1depth++;
	}
	for(j = 0 ; p2->full_path[j] != '\0' ; j++){
		if(p2->full_path[j] == '/')
			p2depth++;
	}
	if(i == 1) p1depth--;
	if(j == 1) p2depth--;

	if(p1depth < p2depth)
		return sort_decreasing ? 1 : -1;
	else if(p1depth > p2depth)
		return sort_decreasing ? -1 : 1;
	else{
		if(sort_decreasing)
			return -1 * strcmp(p1->full_path, p2->full_path);
		else
			return strcmp(p1->full_path, p2->full_path);
	}
}
int fl_uid_cmp(const EachFile*p1, const EachFile*p2, bool sort_decreasing){
	if(p1->uid < p2->uid)
		return sort_decreasing ? 1 : -1;
	else if(p1->uid > p2->uid)
		return sort_decreasing ? -1 : 1;
	else
		return 0;
}
int fl_gid_cmp(const EachFile*p1, const EachFile*p2, bool sort_decreasing){
	if(p1->gid < p2->gid)
		return sort_decreasing ? 1 : -1;
	else if(p1->gid > p2->gid)
		return sort_decreasing ? -1 : 1;
	else
		return 0;
}
int fl_mode_cmp(const EachFile*p1, const EachFile*p2, bool sort_decreasing){
	if(p1->mode < p2->mode)
		return sort_decreasing ? 1 : -1;
	else if(p1->mode > p2->mode)
		return sort_decreasing ? -1 : 1;
	else
		return 0;
}
//global_fileset을 cmp에 맞춰 정렬시킨다.
void file_set_sort(int (*cmp)(const FileList*, const FileList*, bool sort_decreasing), bool sort_decreasing){
	FileListNode *sorted_end = global_fileset.head->next->next, *sorted_cur;
	FileList *key;

	while(sorted_end != global_fileset.tail){
		key = sorted_end->data;
		sorted_cur = sorted_end->prev;
		while(sorted_cur != global_fileset.head){
			if(cmp(sorted_cur->data, key, sort_decreasing) > 0){
				sorted_cur->next->data = sorted_cur->data;
				sorted_cur = sorted_cur->prev;
			}
			else
				break;
		}
		sorted_cur->next->data = key;
		sorted_end = sorted_end->next;
	}
}

/*file_set을 정렬시키는데 사용되는 비교 함수들*/
int fs_filename_cmp(const FileList*f1, const FileList*f2, bool sort_decreasing){
	const EachFile *p1 = f1->head->next->data;
	const EachFile *p2 = f2->head->next->data;
	return fl_filename_cmp(p1, p2, sort_decreasing);
}
int fs_size_cmp(const FileList*p1, const FileList*p2, bool sort_decreasing){
	if(p1->file_size < p2->file_size)
		return sort_decreasing ? 1 : -1;
	else if(p1->file_size > p2->file_size)
		return sort_decreasing ? -1 : 1;
	else
		return 0;
}
int fs_result_cmp(const FileList*p1, const FileList*p2, bool sort_decreasing){
	int val = fs_size_cmp(p1,p2, sort_decreasing);
	if(val != 0)
		return val;
	else
		return fs_filename_cmp(p1,p2,sort_decreasing);
}
int fs_uid_cmp(const FileList*p1, const FileList*p2, bool sort_decreasing){
	if(p1->head->next->data->uid < p2->head->next->data->uid)
		return sort_decreasing ? 1 : -1;
	else if(p1->head->next->data->uid > p2->head->next->data->uid)
		return sort_decreasing ? -1 : 1;
	else
		return 0;
}
int fs_gid_cmp(const FileList*p1, const FileList*p2, bool sort_decreasing){
	if(p1->head->next->data->gid < p2->head->next->data->gid)
		return sort_decreasing ? 1 : -1;
	else if(p1->head->next->data->gid > p2->head->next->data->gid)
		return sort_decreasing ? -1 : 1;
	else
		return 0;
}
int fs_mode_cmp(const FileList*p1, const FileList*p2, bool sort_decreasing){
	if(p1->head->next->data->mode < p2->head->next->data->mode)
		return sort_decreasing ? 1 : -1;
	else if(p1->head->next->data->mode > p2->head->next->data->mode)
		return sort_decreasing ? -1 : 1;
	else
		return 0;
}

//global_fileset에 있는 모든 filelist 들을 cmp에 맞춰 정렬시킨다.
void all_file_list_sort(int (*cmp)(const EachFile*, const EachFile*, bool dec), bool decresing){
	FileListNode *cur = global_fileset.head->next;
	while(cur != global_fileset.tail){
		file_list_sort(cur->data, cmp, decresing);
		cur = cur->next;
	}
}

//index번째 중복 FileListNode를 구한다.
//index는 1부터 시작한다.
FileListNode *get_filelistnode_with_index(int index){
	if(index > global_fileset.node_cnt || index < 1){
		return NULL;
	}
	FileListNode *target = global_fileset.head->next;
	int i = 0;
	while(target != global_fileset.tail){
		if(target->data->file_count >= 2){
			++i;
			if(i == index){
				break;
			}
		}
		target = target->next;
	}
	if(target == global_fileset.tail)
		return NULL;
	return target;
}

//index번째 중복 FileList를 구한다.
//index는 1부터 시작한다.
FileList *get_filelist_with_index(int index){
	FileListNode *fln = get_filelistnode_with_index(index);
	if(fln == NULL)
		return NULL;
	return fln->data;
}

//help 출력
void print_help(){
	puts("Usage:");
	puts(" > fsha1 -e [FILE_EXTENSION] -l [MINSIZE] -h [MAXSIZE] -d [TARGET_DIRECTORY] -t [THREAD_NUM]");
	puts("      >> delete -l [SET_INDEX] -d [OPTARG] -i -f -t");
	puts(" > list -l [LIST_TYPE] -c [CATEGORY] -o [ORDER]");
	puts(" > trash -c [CATEGORY] -o [ORDER]");
	puts(" > restore [RESTORE_INDEX]");
	puts(" > help");
	puts(" > exit\n");
}

/*index string을 index 정수로 리턴함
 * 변환이 불가능하면 -1 리턴*/
int get_idx(const char *str){
	if(strlen(str) == 0)
		return -1;
	for(int k = 0 ; k < (int)strlen(str) ; k++){
		if(!isdigit(str[k])){
			return -1;
		}
	}
	return atoi(str);
}

//fsha1의 인자로 들어온 조건 정보를 저장하는 구조체
typedef struct{
	char extension[256];
	long long minsize;
	long long maxsize;
	int max_thread_count;
}CheckerData;
CheckerData checker;
bool is_checker_valid = false;

//fsha1 명령 실행
int fsha1(int argc, char **argv);

//list 명령 실행
int list(int argc, char **argv){
	if(get_duplicate_cnt() <= 0){//검색된 중복 파일이 없을 경우
		puts("fileset이 비어 있습니다.");
		return 1;
	}
	extern char *optarg;
	extern int optopt;
	extern int optind;
	int opt = 0;
	optind = 0;

	char listtype = '\0';
	char category = '\0';
	int order = 0;
	while(1){
		opt = getopt(argc, argv, "l:c:o:");
		if(opt == -1)
			break;
		switch(opt){
			case 'l':
				if(listtype != '\0'){
					printf("중복된 옵션 -c\n");
					return -2;
				}
				if(strcmp(optarg,"filelist")==0)
					listtype = 'l';
				else if(strcmp(optarg,"fileset")==0)
					listtype = 's';
				else{
					puts("[LIST_TYPE]은 fileset, filelist 중 하나여야 합니다.");
					return -1;
				}
				break;
			case 'c':
				if(category != '\0'){
					printf("중복된 옵션 -c\n");
					return -2;
				}
				if(strcmp(optarg,"filename")==0)
					category = 'f';
				else if(strcmp(optarg,"size")==0)
					category = 's';
				else if(strcmp(optarg,"uid")==0)
					category = 'u';
				else if(strcmp(optarg,"gid")==0)
					category = 'g';
				else if(strcmp(optarg,"mode")==0)
					category = 'm';
				else{
					puts("[CATEGORY]는 filename, size, uid, gid, mode중 하나여야 합니다.");
					return -1;
				}
				break;
			case 'o':
				if(order != 0){
					printf("중복된 옵션 -o\n");
					return -2;
				}
				if(strcmp(optarg,"-1")==0)
					order = -1;
				else if(strcmp(optarg,"1")==0)
					order = 1;
				else{
					puts("[ORDER]은 -1, 1중 하나여야 합니다.");
					return -1;
				}
				break;
			default:
				printf("잘못된 옵션 : %c\n", optopt);
				return -3;
		}
	}
	bool deorder = (order == -1) ? true : false;
	if(listtype == 'l'){//file list 정렬
		switch(category){
			case '\0':
			case 's':
				puts("filelist에는 같은 사이즈의 파일만 있습니다. fileset에 대해 정렬합니다.");
				file_set_sort(fs_size_cmp, deorder);
				break;
			case 'f':
				all_file_list_sort(fl_filename_cmp, deorder);
				break;
			case 'u':
				all_file_list_sort(fl_uid_cmp, deorder);
				break;
			case 'g':
				all_file_list_sort(fl_gid_cmp, deorder);
				break;
			case 'm':
				all_file_list_sort(fl_mode_cmp, deorder);
				break;
		}
	}
	else{//file set 정렬(기본값)
		switch(category){
			case '\0':
			case 's':
				file_set_sort(fs_size_cmp, deorder);
				break;
			case 'f':
				file_set_sort(fs_filename_cmp, deorder);
				break;
			case 'u':
				file_set_sort(fs_uid_cmp, deorder);
				break;
			case 'g':
				file_set_sort(fs_gid_cmp, deorder);
				break;
			case 'm':
				file_set_sort(fs_mode_cmp, deorder);
				break;
		}
	}
	print_fileset();
	return 0;
}

//trash 명령 수행 함수
//휴지통 리스트에 있는 정보를 정렬해서 출력
int trash(int argc, char **argv){
	extern char *optarg;
	extern int optopt;
	extern int optind;
	int opt = 0;
	optind = 0;
	char category='\0';
	int order = 0;
	while(1){
		opt = getopt(argc, argv, "c:o:");
		if(opt == -1)
			break;
		switch(opt){
			case 'c':
				if(category != '\0'){
					printf("중복된 옵션 -c\n");
					return -2;
				}
				if(strcmp(optarg,"filename")==0)
					category = 'f';
				else if(strcmp(optarg,"size")==0)
					category = 's';
				else if(strcmp(optarg,"date")==0)
					category = 'd';
				else if(strcmp(optarg,"time")==0)
					category = 't';
				else{
					puts("[CATEGORY]는 filename, size, date, time중 하나여야 합니다.");
					return -1;
				}
				break;
			case 'o':
				if(order != 0){
					printf("중복된 옵션 -o\n");
					return -2;
				}
				if(strcmp(optarg,"-1")==0)
					order = -1;
				else if(strcmp(optarg,"1")==0)
					order = 1;
				else{
					puts("[ORDER]은 -1, 1중 하나여야 합니다.");
					return -1;
				}
				break;
			default:
				printf("잘못된 옵션 : %c\n", optopt);
				return -3;
		}
	}
	bool deorder = (order == -1) ? true : false;
	switch(category){
		case '\0':
			trash_list_sort(tl_default_cmp, deorder);
			break;
		case 'f':
			trash_list_sort(tl_filename_cmp, deorder);
			break;
		case 's':
			trash_list_sort(tl_size_cmp, deorder);
			break;
		case 't':
			trash_list_sort(tl_time_cmp, deorder);
			break;
		case 'd':
			trash_list_sort(tl_date_cmp, deorder);
			break;
	}
	print_trash_list();
	return 0;
}

//인자로 들어온 name, filesize가 찾고자 하는 파일의 조건에 충족되는가
bool is_collectable(const CheckerData* checker_data, const char* name, long long filesize);

//trash list의 파일을 원래 경로로  복원
int restore(int argc, char **argv){
	if(argc != 2){
		printf("usage : %s [RESTORE_INDEX]\n", argv[0]);
		return -1;
	}
	if(global_trash_list.node_cnt <= 0){
		printf("Trash bin is empty\n");
		return -2;
	}
	int idx = get_idx(argv[1]);
	if(idx == -1){
		printf("[RESTORE_INDEX] 오류\n");
		return -1;
	}
	TrashNode *trashnode = get_trashnode_with_index(idx);
	if(trashnode == NULL){
		printf("[RESTORE_INDEX]가 범위를 벗어났습니다.\n");
		return -1;
	}
	char trashfile_path[4097];
	char restored_path[4097];
	strcpy(trashfile_path, trash_bin_files_path);
	strcat(trashfile_path, "/");
	strcat(trashfile_path, trashnode->data->alias);//쓰레기통에 있는 파일 이름
	struct stat64 stbuf;
	stat64(trashfile_path, &stbuf);//쓰레기통에 있는 파일의 stat 받아오기

	strcpy(restored_path, trashnode->data->real_path);//복원 경로
	char *numpt = restored_path + strlen(restored_path);
	int num=1;
	while(access(restored_path, F_OK) == 0)//파일이 존재하면 이름 뒤에 정수를 붙임
		sprintf(numpt, "_%d", ++num);
	if(rename(trashfile_path, restored_path) != 0){//휴지통 -> 원래 경로
		fprintf(stderr, "rename(%s, %s) error\n", trashfile_path, restored_path);
		return -2;
	}
	
	/*원래 파일셋에 돌려놓는 작업*/
	/*fsha1이 이전에 수행되지 않았다면 fileset은 비어있으므로 이 작업은 건너뛰게된다.*/
	if(is_checker_valid && is_collectable(&checker, restored_path, stbuf.st_size)){//fileset의 조건에 해당한다면
		EachFile *file_data = make_eachfile(restored_path, &stbuf);//새 파일 정보를 만든다.
		unsigned char *hash;
		if(stbuf.st_size == 0){
			hash = NULL;
		}
		else{
			hash = (unsigned char *)malloc(HASH_SIZE);
			memcpy(hash, trashnode->data->hash, HASH_SIZE);
		}
		file_set_sort(fs_size_cmp, false);//기존 파일 세트 정렬
		insert_to_glob_fileset(file_data, stbuf.st_size, hash);//삽입
		all_file_list_sort(fl_filename_cmp, false);//filelist는 name에 따라 정렬
		file_set_sort(fs_result_cmp, false);//fileset 전체 정렬
	}
	if(num != 1)
		printf("%s가 이미 있어 %s로 복원하였습니다.\n", trashnode->data->real_path, restored_path);
	time_t now = time(NULL);
	wlog(RST, restored_path, &now); //로그 기록
	printf("[%s] success for %s\n", RST, restored_path);
	delete_trashnode(trashnode, save_trash_list);
	print_trash_list();
	return 0;
}

//학번 출력 프롬프트
void main_prompt(){
	char input[8196];
	char *tok;
	char *argv[30] = {0};
	int argc;

	while(1){
		argc = 0;
		
		printf("20172644> "); //학번 출력
		fgets(input, sizeof(input), stdin); //입력
		/*토크나이징*/
		tok = strtok(input, " \n"); 
		while(tok != NULL &&
				(size_t)argc < (sizeof(argv)/sizeof(char*))
				){
			argv[argc++] = tok;
			tok = strtok(NULL, " \n");
		}
		if(argc != 0){
			if(strcmp(argv[0], "fsha1") == 0){
				fsha1(argc, argv);
			}
			else if(strcmp(argv[0], "list") == 0){
				list(argc, argv);
			}
			else if(strcmp(argv[0], "trash") == 0){
				trash(argc, argv);
			}
			else if(strcmp(argv[0], "restore") == 0){
				restore(argc, argv);
			}
			else if(strcmp(argv[0], "exit") == 0){
				puts("Prompt End");
				exit(0);
			}
			else if(strcmp(argv[0], "help") == 0){
				print_help();
			}
			else{
				print_help(); //이외의 명령어는 help 호출
			}
		}
	}
}


//file_size를 조건에 맞는지 검사하는 함수
bool _is_size_in_range(const CheckerData* checker_data, long long filesize){
	/*filesize가 범위내에 있는지 검사*/
	if(checker_data->minsize == (long long)-1 && 
			checker_data->maxsize == (long long)-1)//All files
		return true;
	else if(checker_data->minsize == (long long)-1)//min size 제한이 없을때
		return filesize <= checker_data->maxsize;
	else if(checker_data->maxsize == (long long)-1)//max size 제한이 없을때
		return filesize >= checker_data->minsize;
	else
		return (filesize >= checker_data->minsize && filesize <= checker_data->maxsize);
}

//checker_data를 이용해 파일 name과 file_size가 조건에 맞는지 검사
bool is_collectable(const CheckerData* checker_data, const char* name, long long filesize){
	//file size 검사
	if(!_is_size_in_range(checker_data, filesize))
		return false;
	/* 모든 파일 허용 */
	else if(checker_data->extension[0] == '\0')
		return true;
	else{
		/* 확장자 ptr+1를 checker_data의 확장자와 비교*/
		const char* ptr = strrchr(name, '.');
		if(ptr == NULL)
			return false;
		else if(strcmp(ptr+1, checker_data->extension) == 0) //확장자 일치
			return true;
		else
			return false;
	}
}

//scandir filter
static int scan_filter(const struct dirent *dirent){
	if(strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0){
		//., .. 디렉토리는 pass
		return 0;
	}
	else if(dirent->d_type != DT_DIR && dirent->d_type != DT_REG){
		//정규 파일/디렉토리 파일이 아니면 pass
		return 0;
	}
	else
		return 1;
}

/*탐색 금지 디렉토리인지 검사하는 함수*/
bool is_inaccessible_dir(const char *full_dir_path){
	const char *inaccessible[]={
		"/proc",
		"/run",
		"/sys",
		trash_bin_files_path,
		trash_bin_info_path
	};
	/*탐색 금지 디렉토리로 시작하는 path이면 true 리턴*/
	for(size_t i = 0 ; i < sizeof(inaccessible)/sizeof(char*) ; i++){
		if(strncmp(full_dir_path, inaccessible[i], strlen(inaccessible[i])) == 0){
			return true;
		}
	}
	return false;
}

PathQueue *path_queue;
typedef struct thread_memory{
	int workstatus;
	char q_path[4097];
	char* nulpos;
	int entcnt;
	struct dirent **dirents;
	struct stat64 st;
	unsigned char *hash;
}ThreadMemory;
ThreadMemory t_data[MAX_THREAD]; //thread memory
pthread_t thread_arr[MAX_THREAD]; //thread array

//모든 쓰레드가 wait상태인가
bool check_allstop(){
	for(int i = 0 ; i < checker.max_thread_count; i++){
		if(t_data[i].workstatus != 0)
			return false;
	}
	return true;
}

//모든 쓰레드에 대해 종료 플래그 설정
#define WS_FINISH 2
void set_finish(){
	for(int i = 0 ; i < checker.max_thread_count; i++){
		t_data[i].workstatus = WS_FINISH;
	}
}

//thread 함수
void *thread_search(void *mem){
	ThreadMemory *td = (ThreadMemory *)(mem);
	int isempty; 
	while(1){
		pthread_mutex_lock(&queue_mutex); 
		isempty = is_queue_empty(path_queue);//큐가 비어있는지 확인

		if(isempty){
			pthread_mutex_unlock(&queue_mutex); //큐 뮤텍스를 푼다.
			pthread_mutex_lock(&cond_mutex); 
			td->workstatus = 0; //대기 상태로 들어감
			if(check_allstop()){ //모두가 대기 상태라면
				set_finish();//모든 작업이 끝난 것이다.
				pthread_mutex_unlock(&cond_mutex);
				pthread_cond_broadcast(&cond);//모두 깨운다.
				return NULL;
			}
			else{
				pthread_cond_wait(&cond, &cond_mutex);//대기
				pthread_mutex_unlock(&cond_mutex);
				//일어났다면
				if(td->workstatus == WS_FINISH){//작업이 끝났다면
					return NULL;
				}
			}
		}
		else{
			td->workstatus = 1; //작업 상태로 들어감
			dequeue(path_queue, td->q_path); //큐에서 path를 꺼낸다.
			pthread_mutex_unlock(&queue_mutex); //큐 뮤텍스를 푼다.
			break;//작업 시작
		}
	}

	td->nulpos = td->q_path + strlen(td->q_path);//basename이 저장될 위치
	td->entcnt = scandir(td->q_path, &(td->dirents), scan_filter, alphasort);//검색된 파일수

	if(td->entcnt == -1){//검색 실패
		fprintf(stderr, "scandir error. path:%s\n", td->q_path);
	}
	else{
		/*디렉토리 내 파일마다 체크*/
		for(int i = 0 ; i < td->entcnt; i++){
			if(*(td->nulpos-1) == '/')//root의 경우
				strcpy(td->nulpos, td->dirents[i]->d_name);//basename 붙이기
			else
				sprintf(td->nulpos, "/%s", td->dirents[i]->d_name);//슬래시 붙이기

			//디렉토리라면path queue에 enqueue
			if(td->dirents[i]->d_type == DT_DIR){
				if(!is_inaccessible_dir(td->q_path)){ //탐색 금지 디렉토리가 아니면
					pthread_mutex_lock(&queue_mutex); 
					enqueue(path_queue, td->q_path); //큐에 삽입
					pthread_mutex_unlock(&queue_mutex); 
				}
			}
			//파일이라면
			else{
				//파일 stat 불러오기
				if(stat64(td->q_path, &(td->st)) != 0){//에러 처리
					fprintf(stderr, "[stat64() error.]\n");
					fprintf(stderr, "  path:%s %s\n", td->q_path, strerror(errno));
				}
				//파라미터 조건들을 만족하는가?
				else if(is_collectable(&checker, td->dirents[i]->d_name, td->st.st_size)){
					if(td->st.st_size != 0){ //size>0이면 해쉬를 구함.
						td->hash = (unsigned char *)malloc(HASH_SIZE);
						get_hash(td->q_path, td->hash);
					}
					else//size == 0이면 해쉬를 구하지 않는다.
						td->hash = NULL;

					pthread_mutex_lock(&fileset_mutex); //파일 정보 삽입에 대한 뮤텍스 잠금
					insert_to_glob_fileset(make_eachfile(td->q_path, &(td->st)),
							td->st.st_size, td->hash); //FileSet에 파일 정보 삽입
					pthread_mutex_unlock(&fileset_mutex); //뮤텍스 잠금 해제
					td->hash = NULL;
				}
			}
			*(td->nulpos) = '\0';//원래 디렉토리 경로로 복구
			free(td->dirents[i]);
		}
		free(td->dirents);
	}

	pthread_cond_broadcast(&cond);//모든 쓰레드 깨우기
	thread_search(mem);//재귀적으로 동작
	return NULL;
}

//root_path부터 쓰레드를 이용하여 탐색하는 함수
void start_search(const char *root_path){
	path_queue = make_path_queue();
	enqueue(path_queue, root_path); //큐에 root_path 삽입
	int i;
	memset(t_data, 0, sizeof(ThreadMemory)*checker.max_thread_count);
	for(i = 0 ; i < checker.max_thread_count ; i++){
		t_data[i].workstatus = -1; //thread 상태 변수 초기화
	}
	for(i = 0 ; i < checker.max_thread_count ; i++){
		switch(i){
			case 0:
				pthread_create(&thread_arr[0], NULL, thread_search, t_data);
				break;
			case 1:
				pthread_create(&thread_arr[1], NULL, thread_search, t_data+1);
				break;
			case 2:
				pthread_create(&thread_arr[2], NULL, thread_search, t_data+2);
				break;
			case 3:
				pthread_create(&thread_arr[3], NULL, thread_search, t_data+3);
				break;
			case 4:
				pthread_create(&thread_arr[4], NULL, thread_search, t_data+4);
				break;
		}
	}
	for(i = 0 ; i < checker.max_thread_count ; i++){
		pthread_join(thread_arr[i], NULL);
	}
	delete_path_queue(path_queue);
}


//시작 시간과 종료  시간을 받아  초:마이크로초로 출력
void print_runtime(const struct timeval *st, const struct timeval *ed){
	long sec = ed->tv_sec - st->tv_sec;
	long usec = ed->tv_usec - st->tv_usec;
	if(usec < 0){
		sec--;
		usec += 1000000; //초를 마이크로초로 환산하여 더함
	}
	printf("Searching time: %ld:%06ld(sec:usec)\n", sec, usec);
}

/*KB, MB, GB 가 포함된 사이즈 문자열을 long long로 변환후 리턴(바이트 단위)
 *실패시 음수값 리턴 */
int unitsize_to_offt(long long *result, const char* sizestr){
	/*사이즈 스트링의 길이가 2이하라면*/
	size_t len = strlen(sizestr);
	if(len == 0)
		return -4;
	if(len <= 2){//단위가 올 수 없을 정도로 짧은 경우
		/*전부 숫자여야만 허용*/
		for(int i = 0 ; i < (int)len ; i++)
			if(!isdigit(sizestr[i]))
				return -1;
		*result = atoll(sizestr);
		return 0;
	}
	else {
		size_t numstrlen = len-2; //unit을 제외한 부분의 길이
		int flag = -1; //unit flag
		int point = -1; //소수점 위치

		/*unit을 제외한 sizestr이 실수인지 체크*/
		for(size_t i = 0 ; i < numstrlen ; i++){
			if(isdigit(sizestr[i]))
				continue;
			else if(point == -1 && sizestr[i] == '.') //소수점 최초 등장시 위치 저장.
				point = i;
			else
				return -2;
		}
		char unit[3] = {0}; //unit string 저장 배열
		strcpy(unit, sizestr+numstrlen); //unit string 복사
		if(isdigit(unit[0]) && isdigit(unit[1]))
			flag = 0; //unit 없음
		else {
			/*unit에 따라 flag 설정*/
			unit[0] = toupper(unit[0]); //전부 대문자로
			unit[1] = toupper(unit[1]);
			if(strcmp(unit, "KB") == 0)
				flag = 1;
			else if(strcmp(unit, "MB") == 0)
				flag = 2;
			else if(strcmp(unit, "GB") == 0)
				flag = 3;
			else
				return -3;
		}
		/*string to long long*/
		char pointback[10] = {0}; //소수점 뒷자리 저장을 위한 임시 배열
		*result = atoll(sizestr); //소수점 앞자리 저장
		for(int i = 0 ; i < flag*3 ; i++)
			*result *= 10; //단위별 곱셈
		if(point != -1){ //소수점 처리
			const char *pt = sizestr+point+1;
			int i = 0;
			while(i < flag*3 && *pt != '\0' && isdigit(*pt))
				pointback[i++] = *pt++;
			while(i < flag*3)
				pointback[i++] = '0';
			pointback[i] = '\0';
			*result += atoll(pointback);
		}
		return 0;
	}
}

//처음 ~ 문자를 home directory로 변환하는 함수
char *replace_home_dir(const char *origin, char *dst){
	if(strcmp(origin, "~") == 0)
		strcpy(dst, getenv("HOME"));
	else if(strncmp(origin, "~/", 2) == 0)
		sprintf(dst, "%s%s", getenv("HOME"), origin+1);
	else
		strcpy(dst, origin);
	return dst;
}
#define NOERR 0
#define EXT_ERR 1
#define MIN_ERR 2
#define MAX_ERR 4
#define TOOMANY_THREAD 8
#define THREAD_COUNT_ERR 16
#define DIR_ERR 32
#define NEED_PARAM 64
#define MINOVERMAX 128
#define OVERFLOW 256
#define ZERO_THREAD 512
#define WRONG_PARAM 1024
#define UNKNOWN_PARAM 2048

//fsha1의 옵션을 검사하여 dst에 저장
int fsha1_option_check(int argc, char **argv, CheckerData *dst, char *root_path){
#define _EOPT 1
#define _LOPT 2
#define _HOPT 4
#define _DOPT 8
#define _TOPT 16
	extern char *optarg;
	extern int optopt;
	extern int optind;
	int res;
	int opt = 0;
	unsigned optbit = 0; //옵션 비트 저장 변수
	char tempbuf[4097] = {0};
	struct stat statbuf;
	optind = 0;
	CheckerData cp;
	while(1){
		opt = getopt(argc, argv, "e:l:h:d:t:");
		if(opt == -1)
			break;
		switch(opt){
			case 'e':
				if(optbit & _EOPT){
					sprintf(root_path, "중복된 옵션 -e");
					return WRONG_PARAM;
				}
				if(strcmp(optarg, "*") == 0)
					cp.extension[0] = '\0';
				else if(strncmp(optarg, "*.", 2) == 0)
					strncpy(cp.extension, optarg+2, sizeof(cp.extension));
				else{
					sprintf(root_path, "FILE_EXTENSION is wrong.");
					return EXT_ERR;
				}
				optbit |= _EOPT;
				break;
			case 'l':
				if(optbit & _LOPT){
					sprintf(root_path, "중복된 옵션 -l");
					return WRONG_PARAM;
				}
				if(strcmp(optarg, "~") == 0)
					cp.minsize = -1;
				else{
					res = unitsize_to_offt(&(cp.minsize), optarg);
					if(res != 0){
						sprintf(root_path, "MINSIZE is wrong.");
						return MIN_ERR;
					}
				}
				optbit |= _LOPT;
				break;
			case 'h':
				if(optbit & _HOPT){
					sprintf(root_path, "중복된 옵션 -h");
					return WRONG_PARAM;
				}
				if(strcmp(optarg, "~") == 0)
					cp.maxsize = -1;
				else{
					res = unitsize_to_offt(&(cp.maxsize), optarg);
					if(res != 0){
						sprintf(root_path, "MAXSIZE is wrong.");
						return MAX_ERR;
					}
				}
				optbit |= _HOPT;
				break;
			case 'd':
				if(optbit & _DOPT){
					sprintf(root_path, "중복된 옵션 -d");
					return WRONG_PARAM;
				}
				replace_home_dir(optarg, tempbuf);
				if(realpath(tempbuf, root_path) == NULL){
					sprintf(root_path, "target directory error. %s", strerror(errno));
					return DIR_ERR;
				}
				if(stat(root_path, &statbuf) < 0){
					sprintf(root_path, "target directory error. %s", strerror(errno));
					return DIR_ERR;
				}
				if(S_ISDIR(statbuf.st_mode) == 0){
					sprintf(root_path, "[TARGET_DIRECTORY] is not a directory.");
					return DIR_ERR;
				}
				optbit |= _DOPT;
				break;
			case 't':
				if(optbit & _TOPT){
					sprintf(root_path, "중복된 옵션 -t");
					return WRONG_PARAM;
				}
				/*인자 오류 체크*/
				for(int i = 0 ; (size_t)i < strlen(optarg) ; i++)
					if(!isdigit(optarg[i])){
						sprintf(root_path, "THREAD_NUM is wrong.");
						return THREAD_COUNT_ERR;
					}
				cp.max_thread_count = atoi(optarg);
				/*쓰레드는 최대 5*/
				if(cp.max_thread_count <= 0){
					sprintf(root_path, "minimum number of thread is 1");
					return ZERO_THREAD;
				}
				if(cp.max_thread_count > MAX_THREAD){
					sprintf(root_path, "maximum number of thread is %d", MAX_THREAD);
					return TOOMANY_THREAD;
				}
				optbit |= _TOPT;
				break;
			default:
				sprintf(root_path, "unknown option \'%c\'", optopt);
				return UNKNOWN_PARAM;
		}
	}
	//check necessary option 
	if(((~_TOPT & optbit) ^ 15) != 0){
		tempbuf[0] = '\0';
		if(!(optbit & _EOPT))
			strcat(tempbuf, "-e [FILE_EXTENSION] ");
		if(!(optbit & _LOPT))
			strcat(tempbuf, "-l [MINSIZE] ");
		if(!(optbit & _HOPT))
			strcat(tempbuf, "-h [MAXSIZE] ");
		if(!(optbit & _DOPT))
			strcat(tempbuf, "-d [TARGET_DIRECTORY] ");
		tempbuf[strlen(tempbuf)-1] = '\0';
		sprintf(root_path, "option %s is necessary.", tempbuf);
		return NEED_PARAM;
	}
	if(cp.minsize != -1 && cp.maxsize != -1 &&
			cp.minsize > cp.maxsize){
		sprintf(root_path, "MINSIZE가 MAXSIZE보다 큽니다.");
		return MINOVERMAX; //minsize가 maxsize보다 큼
	}
	//overflow check
	if(cp.minsize < -1){
		sprintf(root_path, "minsize overflowed.");
		return OVERFLOW;
	}
	if(cp.maxsize < -1){
		sprintf(root_path, "maxsize overflowed.");
		return OVERFLOW;
	}
	//t 옵션을 입력하지 않으면 기본 thread 개수는 1임.
	if(!(optbit & _TOPT)){
		if(argc != 9){
			sprintf(root_path, "잘못 입력하셨습니다.");
			return UNKNOWN_PARAM;
		}
		cp.max_thread_count = 1;
	}
	else if(argc != 11){
		sprintf(root_path, "잘못 입력하셨습니다.");
		return UNKNOWN_PARAM;
	}
	memcpy(dst, &cp, sizeof(CheckerData));
	return 0;
}

//중복 파일 관리 프롬프트
int inner_prompt();

//fsha1 명령 수행 함수
int fsha1(int argc, char **argv){
	char root_path[4097];
	memset(&checker, 0, sizeof(CheckerData));
	int option_check_result = fsha1_option_check(argc, argv, &checker, root_path);
	if(option_check_result != 0){ //option is wrong
		printf("option error. %s\n", root_path);
	}
	else{
		struct timeval st, ed;
		if(global_fileset.node_cnt != -1)
			free_fileset(&global_fileset);
		init_fileset(&global_fileset);//global_fileset 초기화
		is_checker_valid = true;
		gettimeofday(&st, NULL); //시작 시간 저장

		start_search(root_path);//검색
		all_file_list_sort(fl_filename_cmp, false);
		file_set_sort(fs_result_cmp, false);

		gettimeofday(&ed, NULL); //종료 시간 저장
		
		int dupcnt = get_duplicate_cnt();
		if(dupcnt == 0)//검색된 중복 파일이 없을 경우
			printf("No duplicates in %s\n", root_path);
		else 
			print_fileset();
		print_runtime(&st, &ed); //검색 시간 출력
		putchar('\n');
		if(dupcnt > 0){//검색된 중복 파일이 있는 경우
			inner_prompt();//내장 프롬프트 수행
		}
	}
	return option_check_result;
}

//제거 데이터
typedef struct {
	char *path;
	long long size;
	unsigned char *hash;
} RemoveData;

/*path에 해당하는 파일 제거*/
bool remove_file(RemoveData *rdata){
	int res = remove(rdata->path);
	if(res != 0){
		printf("remove error:%s\n", strerror(errno));
		return false;
	}
	time_t now = time(NULL);
	wlog(DEL, rdata->path, &now);
	return true;
}

/*path에 해당하는 파일을 휴지통 경로로 옮김*/
bool move_to_trash_bin(RemoveData *rdata){
	char trash_bin_file[4097];
	char *cur = trash_bin_file, *alias;
	strcpy(cur, trash_bin_files_path);
	cur += strlen(trash_bin_files_path);
	*cur++ = '/';
	alias = cur;
	if(rdata->hash == NULL){
		cur += sprintf(cur, ".ZEROFILE");// 파일 크기가 0인 경우
	}
	else{
		cur += sprintf(cur, ".");
		for(int i = 0 ; i < HASH_SIZE ; i++)
			cur += sprintf(cur, "%02x", rdata->hash[i]);//휴지통 내에서는 파일 이름이 해쉬가 됨
	}
	int num = 1;
	while(access(trash_bin_file, F_OK) == 0)//파일이 존재하면 이름 뒤에 정수를 붙임
		sprintf(cur, "_%d", ++num);
	time_t now = time(NULL);
	if(rename(rdata->path, trash_bin_file) != 0){
		fprintf(stderr,"trashbin error. %s\n", strerror(errno));
		return false;
	}
	add_trash_info(make_trashinfo(rdata->path, alias, &now, rdata->size, rdata->hash), save_trash_list);
	wlog(RM, rdata->path, &now);
	return true;
}

/*d 옵션 함수
 * setidx의 listidx번째 파일을 삭제하는 함수*/
int _d_option(int setidx, int listidx){
	FileList *filelist = get_filelist_with_index(setidx);
	if(filelist == NULL){
		puts("[SET_IDX]의 범위를 벗어났습니다.");
		return -3;
	}
	if(listidx <= 0 || listidx > filelist->file_count){
		puts("[LIST_IDX]의 범위를 벗어났습니다.");
		return -1;
	}
	EachFileNode *target = filelist->head;
	for(int i = 0 ; i < listidx ; i++)
		target = target->next;
	if(target == filelist->head || target == filelist->tail){
		printf("error:_d_option{target == head or tail}\n");
		return -2;
	}
	else{
		RemoveData rdata = {target->data->full_path, 0, NULL};
		if(!remove_file(&rdata))
			printf("ERROR! while removing %s\n", target->data->full_path);
		else
			printf("\"%s\" has been deleted in #%d\n", target->data->full_path, setidx);
		delete_each_file_node(target);
		filelist->file_count--;
		return 0;
	}
}

/*i 옵션 함수
 * setidx의 파일 리스트의 파일을 하나하나 체크하며
 * 삭제하는 함수*/
int _i_option(int setidx){
	FileListNode *node = get_filelistnode_with_index(setidx);
	if(node == NULL){
		puts("[SET_IDX]의 범위를 벗어났습니다.");
		return -3;
	}
	FileList *filelist = node->data;
	EachFileNode *target = filelist->head->next;
	char input[4];
	while(target != filelist->tail){
		printf("Delete \"%s\"? [y/n] ", target->data->full_path);
		fgets(input, sizeof(input), stdin);
		if(input[strlen(input)-1] != '\n')//너무 긴 문자열을 입력받은 경우
			while(getchar() != '\n')//표준입력 버퍼를 비움
				;
		input[0] = toupper(input[0]);//첫글자가 소문자인 경우  대문자로 변경
		if(strcmp(input, "Y\n") == 0){
			RemoveData rdata = {target->data->full_path, 0, NULL};
			if(!remove_file(&rdata))
				printf("ERROR! while removing %s\n", target->data->full_path);
			target = delete_each_file_node(target);//삭제된 노드의 다음 노드로 target에 할당
			filelist->file_count--;
		}
		else if(strcmp(input, "N\n") == 0){
			target = target->next;
		}
		else{
			printf("[입력 오류] y 또는 n만 입력해야 합니다 .");
			return 100;
		}
	}
	if(filelist->file_count == 0){//파일리스트가 비었다면 삭제
		delete_filelist_node(node);
		global_fileset.node_cnt--;
	}
	return 0;
}

/*mtime이 가장 최신인 file path를 제외하고  f를 수행*/
int left_cur_mtime_file(int setidx, bool (*f)(RemoveData*), EachFile *data){
	FileList *filelist = get_filelist_with_index(setidx);
	if(filelist == NULL){
		puts("[SET_IDX]의 범위를 벗어났습니다.");
		return -3;
	}
	EachFileNode *iter = filelist->head->next;
	time_t max_mtime = 0;
	EachFileNode *max_node = iter;
	/*mtime이 가장 큰 노드를 max_node에 할당*/
	while(iter != filelist->tail){
		if(max_mtime < iter->data->mtime){
			max_mtime = iter->data->mtime;
			max_node = iter;
		}
		iter = iter->next;
	}
	iter = filelist->head->next;
	/*max_node를 제외하고 f 수행. f가 false인 경우 에러로 취급.*/
	while(iter != filelist->tail){
		if(iter != max_node){
			RemoveData rdata = {iter->data->full_path, filelist->file_size, filelist->hash};
			if(!f(&rdata))
				printf("ERROR! while removing %s\n", iter->data->full_path);
			iter = delete_each_file_node(iter); //제거된 노드의 다음 노드 할당
			filelist->file_count--;
		}
		else
			iter = iter->next;
	}
	data->mtime = max_node->data->mtime;
	data->full_path = max_node->data->full_path;
	return 0;
}

/*f 옵션 함수
 * setidx의 파일 리스트에서 mtime이 가장 최근인 노드를 제외하고
 * 삭제하는 함수*/
int _f_option(int setidx){
	EachFile leftdata;
	/*left_cur_mtime_file에 remove_file 함수 포인터를 인자로 넘겨서 실행*/
	int rtval = left_cur_mtime_file(setidx, remove_file, &leftdata);
	//남겨진 파일 정보 출력
	printf("Left file in #%d : %s (", setidx, leftdata.full_path);
	print_time(leftdata.mtime);
	printf(")\n");
	return rtval;
}

/*t 옵션 함수
 * setidx의 파일 리스트에서 mtime이 가장 최근인 노드를 제외하고
 * 휴지통으로 옮기는 함수*/
int _t_option(int setidx){
	EachFile leftdata;
	/*left_cur_mtime_file에 move_to_trash_bin함수 포인터를 인자로 넘겨서 실행*/
	int rtval = left_cur_mtime_file(setidx, move_to_trash_bin, &leftdata);
	//남겨진 파일 정보 출력
	printf("All files in #%d have moved to Trash except \"%s\" (", setidx, leftdata.full_path);
	print_time(leftdata.mtime);
	printf(")\n");
	return rtval;
}

//중복 파일 관리 프롬프트
int inner_prompt(){
#define WRONG_COMBINATION_MESSAGE "-d [LIST_IDX], -i, -f, -t 옵션 중 오직 하나만 선택해야 합니다."
	extern char *optarg;
	extern int optopt;
	extern int optind;
	int opt;

	char input[8196];
	char *tok;
	char *argv[30] = {0};
	int argc;

	int setidx;
	int listidx;
	int mode_flag;
	bool wrong;
	while(1){
		argc = 0;
		
		printf(">> "); //학번 출력
		fgets(input, sizeof(input), stdin); //입력
		/*토크나이징*/
		tok = strtok(input, " \n"); 
		while(tok != NULL &&
				(size_t)argc < (sizeof(argv)/sizeof(char*))
				){
			argv[argc++] = tok;
			tok = strtok(NULL, " \n");
		}
		if(argc != 0){
			if(strcmp(argv[0], "delete") == 0){
				optind = 0;
				setidx = -1;
				listidx = -1;
				mode_flag = 0;
				wrong = false;
				
				while(!wrong){
					opt = getopt(argc, argv, "l:d:ift");
					if(opt == -1)
						break;
					switch(opt){
						case 'l':
							if(setidx != -1){
								wrong = true;
								puts("-l 옵션이 중복되었습니다.");
							}
							setidx = get_idx(optarg);
							if(setidx == -1){
								wrong = true;
								puts("잘못 입력하셨습니다.");
							}
							else if(setidx <= 0 || setidx > get_duplicate_cnt()){
								wrong = true;
								puts("[SET_IDX]의 범위를 벗어났습니다.");
							}
							break;
						case 'd':
							if(mode_flag){
								wrong = true;
								puts(WRONG_COMBINATION_MESSAGE);
							}
							if(!wrong){
								listidx = get_idx(optarg);
								if(listidx == -1){
									wrong = true;
									puts("잘못 입력하셨습니다.");
								}
								mode_flag = 'd';
							}
							break;
						case 'i':
							if(mode_flag){
								wrong = true;
								puts(WRONG_COMBINATION_MESSAGE);
							}
							if(!wrong){
								mode_flag = 'i';
							}
							break;
						case 'f':
							if(mode_flag){
								wrong = true;
								puts(WRONG_COMBINATION_MESSAGE);
							}
							if(!wrong){
								mode_flag = 'f';
							}
							break;
						case 't':
							if(mode_flag){
								wrong = true;
								puts(WRONG_COMBINATION_MESSAGE);
							}
							if(!wrong){
								mode_flag = 't';
							}
							break;
						default:
							wrong = true;
							puts("잘못 입력하셨습니다.");
					}
				}
				if(wrong)
					continue;
				else{
					if(setidx == -1 && mode_flag == 0){
						puts("-l [SET_IDX] 옵션과 -d [LIST_IDX], -i, -f, -t 옵션 중 하나를 조합해서 입력하세요.");
						continue;
					}
					else if(setidx == -1){
						puts("-l [SET_IDX] 은 필수 옵션입니다.");
						continue;
					}
					else if(mode_flag == 0){
						puts("-d [LIST_IDX], -i, -f, -t 옵션 중 하나를 입력해야 합니다.");
						continue;
					}
					int returnval = -1;
					if(mode_flag == 'd' && argc == 5)
						returnval = _d_option(setidx, listidx);
					else if(mode_flag == 'i' && argc == 4)
						returnval = _i_option(setidx);
					else if(mode_flag == 'f' && argc == 4)
						returnval = _f_option(setidx);
					else if(mode_flag == 't' && argc == 4)
						returnval = _t_option(setidx);
					else{
						puts("잘못된 입력입니다.");
						continue;
					}
					if(returnval == 0 || returnval == 100){//성공적으로 수행
						putchar('\n');
					}
					else //수행 실패
						continue;
					int dupcnt = get_duplicate_cnt();
					if(returnval == 100){//i option 예외 처리
						if(dupcnt <= 0){
							puts("fileset에 남아있는 중복 파일이 없습니다.");
							return 1;
						}
						else
							continue;
					}
					else if(dupcnt > 0)
						print_fileset();
					else
						return 1;
				}
			}
			else if(strcmp(argv[0], "exit") == 0){
				puts(">> Back to Prompt");
				return 0;
			}
			else{
				printf("잘못 입력하셨습니다.\n");
			}
		}
	}
}


//atexit의 인자로 들어가는 함수
void program_destroy(){
	save_trash_list();
	fclose(__logfile);
	pthread_mutex_destroy(&fileset_mutex);
	pthread_mutex_destroy(&queue_mutex);
	pthread_mutex_destroy(&cond_mutex);
	pthread_cond_destroy(&cond);
}

//프로그램 실행시 초기화 함수
void program_init(){
	static char data_memory[2000];
	char *dst=data_memory;
	uid_t uid = getuid();

	strcpy(dst, getpwuid(uid)->pw_name);
	user_name = dst;//user_name 저장

	dst+=strlen(dst)+1;
	
	if(uid == 0){//root 일경우
		__logfile = fopen("/root/" LOG_FILE_NAME, "a");

		if(__logfile == NULL){
			fprintf(stderr, "%s open failed...\n", "/root/" LOG_FILE_NAME);
			exit(1);
		}

		strcpy(dst, "/root/.Trash/files");
		trash_bin_files_path = dst;//trash_bin_files_path 저장
		dst += strlen(dst)+1;

		strcpy(dst, "/root/.Trash/info");
		trash_bin_info_path = dst;//trash_bin_info_path 저장
		dst += strlen(dst)+1;

		mkdir("/root/.Trash",0755);
		mkdir(trash_bin_files_path,0755);
		mkdir(trash_bin_info_path,0755);
	}
	else{//일반 유저인경우
		sprintf(dst, "/home/%s/%s", user_name, LOG_FILE_NAME);
		__logfile = fopen(dst, "a");

		if(__logfile == NULL){
			fprintf(stderr, "%s open failed...\n", dst);
			exit(1);
		}

		sprintf(dst, "/home/%s/.Trash/files", user_name);
		trash_bin_files_path = dst;//trash_bin_files_path 저장
		dst += strlen(dst)+1;

		sprintf(dst, "/home/%s/.Trash/info", user_name);
		trash_bin_info_path = dst;//trash_bin_info_path 저장
		dst += strlen(dst)+1;

		sprintf(dst, "/home/%s/.Trash", user_name);
		mkdir(dst,0755);
		mkdir(trash_bin_files_path,0755);
		mkdir(trash_bin_info_path,0755);
	}
	sprintf(dst, "%s/.trashinfo", trash_bin_info_path);
	init_trash_list(dst);//trashinfo 파일 경로 저장
	atexit(program_destroy);
}

int main(void){
	program_init();//초기화
	main_prompt();
	exit(0);
}

