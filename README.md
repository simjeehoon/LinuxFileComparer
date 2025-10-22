Linux File Comparer
===
`Linux File Comparer`은
 * **쓰레드**를 이용하여 파일마다 `sha1` 해쉬를 계산해 중복파일을 찾아냅니다. 
 * 중복 파일은 **영구 삭제**하거나 **휴지통**으로 옮길 수 있으며, 휴지통으로 옮겨진 파일은 복원할 수 있습니다.
 * 수행한 삭제/복원 작업에 대해 **로그**가 기록됩니다.

## 사용환경 및 기술스택
- 리눅스/유닉스 환경
- `C언어`, `pthread`, `SHA` 및 `MD5` 해쉬 라이브러리 사용.

## 컴파일
### 1) 라이브러리 설치
`openssl/sha.h` 및 `openssl/md5.h`를 사용합니다. 
- **Linux, Ubuntu** : `sudo apt-get install libssl-dev` 로 라이브러리 설치 필요
- **Linux, Fedora** : `sudo dnf-get install libssl-devel` 로 라이브러리 설치 필요
- **MacOS** : `homebrew` ([https://brew.sh/](https://brew.sh/) 참고) 설치 -> `% brew install openssl`

`pthread` 라이브러리를 사용합니다.

### 2) make 명령어 이용

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image06.png?raw=true" title="image06.png" alt="image06.png"></img><br/>
</center>

`make`를 이용하여 컴파일합니다.<br>
이후 `ssu_sfinder`이 생성됩니다. 이를 실행하면 됩니다.

## 사용 메뉴얼

### 0) 로그확인 방법

이 프로그램은 모든 작업을 로그에 기록합니다. <br>로그 파일의 위치는
- **root**인 경우 `/root/.duplicate_20172644.log` 이며,
- **일반 유저**인 경우 `/home/(사용자명)/.duplicate_20172644.log` 입니다.

로그파일은 아래와 같이 기록됩니다.

> `[COMMAND] [PATH] [DATE] [TIME] [USER NAME]`
>
> - `[COMMAND]` : `REMOVE`, `DELETE`, `RESTORE`
>   * `REMOVE` : 파일이 쓰레기통으로 옮겨진 경우
>   * `DELETE` : 파일이 영구 삭제된 경우
>   * `RESTORE` : 파일이 복구된 경우
>- `[PATH]` : 삭제 및 복원이 수행된 절대 경로
>- `[DATE]` : 삭제 및 복원이 수행된 날짜
>- `[TIME]` : 삭제 및 복원이 수행된 시간
>- `[USER NAME]` : 삭제 및 복원을 수행한 사용자 이름

---

### 1) fsha1 명령 (쓰레드를 이용한 탐색)
해쉬를 이용하여 중복 파일을 탐색합니다.
> `fsha1 –e [FILE_EXTENSION] -l [MINSIZE] -h [MAXSIZE] -d [TARGET_DIRECTORY] -t
[THREAD_NUM]`
> - 지정한 쓰레드 개수(`THREAD_NUM`)를 사용하여
> - 지정한 디렉토리(`TARGET_DIRECTORY`) 및 하위 모든 디렉토리에서
> - 지정한 파일 확장자(`FILE_EXTENSION`)의
> - 특정 크기 사이(`MINSIZE` 이상 `MAXSIZE` 이하)의 파일들 중
> - 중복(해쉬가 같은) 파일을 찾아 리스트로 출력

 - 세부 설명
    - `[FILE_EXTENSION]`

        1. `*` 입력 시, 모든 정규 파일을 대상으로 탐색

        2. `*.(확장자)` 입력 시, `(확장자)`인 정규 파일에서만 중복 파일 탐색 _(예. `*.jpg` : jpg 확장자를 가진 파일만 탐색)_
    - `[MINSIZE]` 및 `[MAXSIZE]`
        
        1. `~` 입력시 다음과 같이 처리

                [MINSIZE]에만 ~ 입력시 [MAXSIZE] 이하인 파일을 탐색
                [MAXSIZE]에만 ~ 입력시 [MINSIZE] 이상인 파일을 탐색
                [MINSIZE], [MAXSIZE]에 ~ 입력시 크기 관계 없이 모든 파일 탐색
        
        
        2. 순수 숫자만 입력시 바이트로 처리.
        
        3. `KB`, `MB`, `GB` 단위 가능. 단, 공백 없이 입력. _ex) 12.34MB_

    - `[TARGET_DIRECTORY]`

       1. 탐색할 디렉토리 경로 _(절대경로, ~(홈 디렉토리), 상대경로 모두 가능)_

       2. 인자에 루트(`/`)를 입력할 경우, root 권한으로 실행.

       3. 루트 디렉토리부터 탐색 시, `proc`과 `run`, `sys` 디렉토리는 제외하여 탐색.

    - `[THREAD_NUM]`

       1. 탐색과정에서 사용할 최대 쓰레드 개수를 정수로 입력 (1이상 5이하)

       2. 기본 값은 1입니다.

#### fsha1 예시
<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image07.png?raw=true" title="image07.png" alt="image07.png"></img><br/>
</center>

 <center>쓰레드 개수를 5로 지정하여 중복 파일을 탐색하는 모습입니다.</center>

### 2) delete 명령 (탐색 이후에 수행 가능)
`fsha1`을 통한 탐색 이후 제거를 수행합니다.
> ` >> delete –l [SET_IDX] -d [LIST_IDX] -i -f -t`  
> `[SET_IDX]`의 `[LIST_IDX]`를 제거
> * `-l [SET_IDX]` : 전체 동일한(중복) 파일 리스트에서 삭제하고자 하는 동일한(중복) 파일 리스트의 세트
번호
> * `-d [LIST_IDX]` : 선택한 세트에서 [LIST_IDX]에 해당하는 파일 삭제
> * `-i` : 선택한 세트의 동일한(중복) 파일 리스트에 있는 파일의 절대 경로를 하나씩 보여주면서 삭제 여부
확인 후 파일 삭제 또는 유지
> * `-f` : 가장 최근에 수정한 파일을 남겨두고 나머지 동일한(중복) 파일을 삭제
> * `-t` : 가장 최근에 수정한 파일을 남겨두고 나머지 동일한(중복) 파일을 휴지통으로 이동

#### delete 예시
 *fsha1 예시*의 1번 세트의 2번 파일을 삭제하기 위해 `delete –l 1 –d 2`를 입력해보겠습니다.

 
<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image08.png?raw=true" title="image08.png" alt="image08.png"></img><br/>
</center>

이후에 `/home/(사용자명)/.duplicate_20172644.log`에 있는 로그 파일에서 확인 가능합니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image09.png?raw=true" title="image09.png" alt="image09.png"></img><br/>
</center>

`delete –l 5 –i` 를 수행해보겠습니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image10.png?raw=true" title="image10.png" alt="image10.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image11.png?raw=true" title="image11.png" alt="image11.png"></img><br/>
</center>

완료.

`delete –l 1 –t`를 입력하여 휴지통에 파일을 넣어보겠습니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image12.png?raw=true" title="image12.png" alt="image12.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image13.png?raw=true" title="image13.png" alt="image13.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image14.png?raw=true" title="image14.png" alt="image14.png"></img><br/>
</center>

완료.

마지막으로 `delete –l 5 –f`를 수행해보겠습니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image15.png?raw=true" title="image15.png" alt="image15.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image16.png?raw=true" title="image16.png" alt="image16.png"></img><br/>
</center>

로그에서 작업이 성공적으로 수행된 것을 확인할 수 있습니다.

---
### 3) list 명령
`list`는 중복 파일 리스트를 출력하는 명령입니다.<br>
`list` 명령은 `fsha1` 명령이 사전에 수행되어야 실행할 수 있습니다.

> `list -l [LIST_TYPE] -c [CATEGORY] -o [ORDER]`  
>> `[LIST_TYPE]`에서 `[CATEGORY]`를 `[ORDER]` 순으로 정렬.  
>> 옵션 생략시 기본값 세팅
* `list` 명령은 사용자가 입력한 정렬 조건에 따라 현재의 파일 관리 링크드리스트를 정렬하고 중복 파일이 존재하는 파일 리스트들을 순차적으로 출력합니다.
* `[LIST_TYPE]`과 `[CATEGORY]`의 조합에 따라 아래의 표와 같이 동작합니다.
   * `[LIST_TYPE]`에는 
      * `fileset` : 동일한(중복) 파일 세트 와
      * `filelist` : 동일한(중복) 파일 리스트 를 쓸 수 있습니다.
      * 기본값은 `fileset`입니다.
   * `[CATEGORY]` 의 목록은 표를 참조해주세요. 기본값은 `size`입니다.
* `[ORDER]`에는 정수 1(오름차순) 혹은 -1(내림차순)을 입력합니다. 기본값은 1입니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/table01.png?raw=true" title="table01.png" alt="table01.png"></img><br/>
</center>

---
### 4) trash 명령
 `trash`는 휴지통에 존재하는 파일 정보를 정렬하여 출력하는 명령어입니다.
> `trash -c [CATEGORY] -o [ORDER]`
> * `[CATEGORY]`를 `[ORDER]` 순으로 정렬. 옵션 생략시 기본값 세팅
> * `[CATEGORY]`에 올 수 있는 목록
>   * `filename` : 파일의 절대 경로
>   * `size` : 파일의 크기
>   * `date` : 파일을 삭제한 날짜
>   * `time` : 파일을 삭제한 시간


 * 프로그램 시작시에 `trash` 정보 파일(휴지통 파일)을 디스크에서 읽습니다.
 * `global_trash_list`에 있는 파일을 주어진 옵션에 따라 정렬하고, 순차적으로 출력합니다.
 * `[ORDER]` 은 1(오름차순) 혹은 -1(내림차순)을 입력합니다. 기본값은 1입니다.
 * 카테고리의 기본값은 파일 이름이며, 정렬 순서의 기본값은 오름차순입니다.
 * `trash`로 출력되는 파일 정보마다 인덱스가 부여되는데 이는 `restore`에서 인자로 사용됩니다.

---
#### list와 trash 예시
<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image17.png?raw=true" title="image17.png" alt="image17.png"></img><br/>
</center>

---
### 5) restore 명령

  `restore` 명령은 휴지통으로부터 복원할 파일을 선택하여 복원하는 명령입니다.

> `restore [RESTORE_INDEX]`
> - `trash` 명령을 통해 `[RESTORE_INDEX]`를 파악합니다. 


#### restore 예시

`restore 2`를 입력해보겠습니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image18.png?raw=true" title="image18.png" alt="image18.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image19.png?raw=true" title="image19.png" alt="image19.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image20.png?raw=true" title="image20.png" alt="image20.png"></img><br/>
</center>

성공적으로 파일이 복원되었습니다.

프로그램을 종료한 뒤에 다시 `trash`를 실행하여도 같은 결과가 나옵니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image21.png?raw=true" title="image21.png" alt="image21.png"></img><br/>
</center>


---
### 6) help 및 exit

- `help`는 사용할 수 있는 명령어와 사용 방법을 표시합니다.
- `exit`를 통해 프로그램을 종료합니다.

---

## 프로그램 구조
fsha1 명령의 동작 구조를 나타낸 것입니다.
<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image02.png?raw=true" title="image02.png" alt="image02.png"></img><br/>
</center>


 1. 이전 베타버전([sha_hash_finder_beta](https://github.com/simjeehoon/sha_hash_finder_beta))과 달리 전역 `fileset`에 중복이 아닌 유일한 파일 정보도 저장됩니다.
    - `restore` 수행 시 해당 파일 정보가 `fileset`에 다시 삽입되게 됩니다.
    이때 원래에는 유일했던 파일이 유일하지 않게 될 수도 있기에 fileset에는 유일한 파일 정보도 저장됩니다.

 2. `start_search` 함수에서 사용자가 입력한 쓰레드 개수만큼 쓰레드를 만들어 검색을 수행합니다. 
 
    쓰레드마다 `thread_search` 함수를 실행하게 됩니다. 아래 그림은 이 함수의 동작 과정을 나타낸 것입니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image03.png?raw=true" title="image03.png" alt="image03.png"></img><br/>
</center>

 1. `thread_search` 함수에서 `path_queue`와 `fileset`은 공유되는 메모리 공간이므로 접근시 mutex_lock을 수행합니다.
 2. 처음에 `path_queue`가 비어있는지 확인하고, 비어있다면 현재 쓰레드의 대기 플래그를 설정합니다.
 3. 그리고 모든 쓰레드의 대기 플래그가 설정되어 있는지 검사합니다.
    * 만약 모든 쓰레드의 대기 플래그가 설정되어 있다면, 모든 디렉터리를 탐색한 것이므로 탐색 종료 플래그를 설정하고 모든 쓰레드를 깨운 뒤 종료합니다.
 4. 어느 하나의 쓰레드라도 대기 플래그가 설정되어 있지 않다면 탐색 작업이 진행 중임을 의미합니다. 작업중인 쓰레드는 `path_queue`에 `path`를 또 추가할 수 있으므로 `ptrhead_cond_wait`을 호출하여 쓰레드를 잠들게 합니다.
 5. 잠든 쓰레드가 시그널을 받아서 깨어났다면, 가장 먼저 작업 종료 플래그의 설정 유무를 검사합니다. 
    1. 작업 종료 플래그가 설정되어 있으면 함수를 종료합니다.
    2. 그렇지 않으면 다시 `path_queue`가 비어있는지 확인한다. 여전히 비어있다면 다시 앞서 설명한 작업을 반복적으로 수행한다.
 6. `path_queue`가 비어있지 않다면 `path_queue`에서 `path` 하나를 꺼내고, 이 `path`에 대해 디렉터리 탐색을 수행합니다.
    1. 정규 파일이 발견되었다면 전역 `CheckerData` 변수를 이용해 해쉬를 구할 정규 파일인지를 판별하고, 해쉬를 구합니다. 
    2. 디렉터리라면 `path_queue`에 삽입합니다. 디렉터리 탐색이 끝났다면 모든 쓰레드를 깨운 뒤 자신을 재귀 호출합니다.


 * 탐색이 끝난 뒤 삭제(`delete`) 명령 프롬프트를 수행합니다. 
 * 휴지통으로 파일을 이동시키는 작업은 [sha_hash_finder_beta](https://github.com/simjeehoon/sha_hash_finder_beta) 에서와 다르게 동작합니다. 아래의 그림은 동작 흐름입니다.

<center>
        <img src="https://github.com/simjeehoon/src_repository/blob/master/LinuxFileComparer/main/image04.png?raw=true" title="image04.png" alt="image04.png"></img><br/>
</center>

 * 휴지통 파일 정보는 메인 메모리의 `global_trash_list`에 저장됩니다.
 * 파일 정보가 추가되거나 삭제될 때마다 `save_trash_list`를 호출하여 변경 내용을 저장합니다. 
 * 휴지통으로 파일을 옮길 경우 원래 파일 이름과 다르게 파일 이름을 설정합니다. 
   * 파일 이름은 휴지통 내에서 해쉬가 유일한 파일인 경우에는 해쉬값이 됩니다.
   * 이미 동일한 해쉬값을 가진 파일이 존재하면 해쉬+정수값이 됩니다.
   * (각 파일은 중복되지 않은 이름을 가져야 하므로 정수값은 파일명이 유일할 때까지 증가합니다.)
   * 단, 파일 크기가 0인 경우에는 해쉬가 없으므로 파일 이름은 `ZEROFILE`이 됩니다. 
   
