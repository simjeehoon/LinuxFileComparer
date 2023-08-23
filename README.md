Linux File Comparer
===
개요
---
 * `Linux File Comparer`은 중복 파일들을 탐색합니다.
 * 다중 쓰레드를 이용하여 파일마다 `sha1` 해쉬를 계산해 중복파일을 찾아냅니다. 
 * 중복 파일은 영구 삭제하거나 휴지통으로 옮길 수 있으며, 휴지통으로 옮겨진 파일은 복원할 수 있습니다.
 * 수행한 삭제/복원 작업에 대해 로그가 기록됩니다.

구현 플랫폼
---
 * uname –a 
 Linux sjh-VirtualBox 5.4.0-107-generic #121~18.04.1-Ubuntu SMP Thu Mar 24 17:21:33 UTC 2022 x86_64 x86_64 x86_64 GNU/Linux
 * Oracle VM VirtualBox, 우분투 18.04.6 LTS

사용 라이브러리 및 헤더
---
 * `openssl/md5.h` 및 `openssl/sha.h`
 * `sudo apt-get install libssl-dev`를 이용하여 라이브러리를 설치함.
 * 32비트 시스템에서 용량이 큰 파일의 정보를 취득하기 위해 `_LARGEFILE64_SOURCE`를 사용하였음.
 * `pthread` 라이브러리를 사용함.

상세 설계
---
전체적인 프로그램 흐름은 아래의 그림과 같다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image01.png?raw=true" title="image01.png" alt="image01.png"></img><br/>
</center>

 1. 프로그램을 실행하면 `program_init()` 함수를 호출한다. 
 2. 쓰레기통에 있는 파일 정보를 불러오고, 로그 파일을 연다. 쓰레기통에 있는 파일 정보는 전역 변수인 `global_trash_list`에 저장된다. 이는 링크드 리스트 구조로 되어있다.
 3. 또한 `atexit`을 이용하여 종료시 쓰레기통 정보를 저장하고, 로그파일을 닫도록 설정한다.
 4. `program_init` 작업이 끝나면 학번 프롬프트를 출력하고 사용자 입력을 받는다. 
 5. `fsha1`, `list`, `trash`, `restore`, `exit`, `help` 명령을 입력할 수 있다. 
 6. 이 외의 명령은 전부 help 명령을 실행한다. 입력받은 명령 종류에 따라 알맞은 함수를 호출한다.

---
### fsha1 명령(쓰레드를 이용한 탐색)

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image02.png?raw=true" title="image02.png" alt="image02.png"></img><br/>
</center>

 1. 검색 조건이 저장되는 `CheckerData`가 전역변수로 선언되어 있다.

 2. 전역 `fileset`에 중복이 아닌 유일한 파일 정보도 저장된다. 이렇게 구현한 목적은 restore 수행 시 조건이 맞다면 해당 파일 정보가 `fileset`에 다시 삽입되게 되는데, 이때 원래에는 유일했던 파일이 유일하지 않게 될 수도 있기 때문이다. 
 3. `start_search` 함수에서 사용자가 입력한 쓰레드 개수만큼 쓰레드를 만들어 검색을 수행한다. 쓰레드마다 `thread_search` 함수를 실행하게 되는데, 아래의 그림은 이 함수의 흐름을 나타낸 것이다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image03.png?raw=true" title="image03.png" alt="image03.png"></img><br/>
</center>

 1. `thread_search` 함수에서 `path_queue`와 `fileset`은 공유되는 메모리 공간이므로 접근시 mutex를 잠근다.
 2. 처음에 `path_queue`가 비어있는지 확인하고, 비어있다면 현재 쓰레드의 대기 플래그를 설정한다. 
 3. 그리고 모든 쓰레드의 대기 플래그가 설정되어 있는지 검사한다. 
    * 만약 모든 쓰레드의 대기 플래그가 설정되어 있다면, 모든 디렉터리를 탐색한 것이므로 탐색 종료 플래그를 설정하고 모든 쓰레드를 깨운 뒤 종료한다.
 4. 어느 하나의 쓰레드라도 대기 플래그가 설정되어 있지 않다면 탐색 작업이 진행 중임을 의미한다. 작업중인 쓰레드는 `path_queue`에 `path`를 또 추가할 수 있으므로 `ptrhead_cond_wait`을 호출하여 쓰레드를 잠들게 한다.
 5. 잠든 쓰레드가 시그널을 받아서 깨어났다면, 가장 먼저 작업 종료 플래그의 설정 유무를 검사한다. 
    1. 작업 종료 플래그가 설정되어 있으면 함수를 종료한다. 
    2. 그렇지 않으면 다시 `path_queue`가 비어있는지 확인한다. 여전히 비어있다면 다시 앞서 설명한 작업을 반복적으로 수행한다.
 6. `path_queue`가 비어있지 않다면 `path_queue`에서 `path` 하나를 꺼내고, 이 `path`에 대해 디렉터리 탐색을 수행한다. 
    1. 정규 파일이 발견되었다면 전역 `CheckerData` 변수를 이용해 해쉬를 구할 정규 파일인지를 판별하고, 해쉬를 구한다. 
    2. 디렉터리라면 `path_queue`에 삽입한다. 디렉터리 탐색이 끝났다면 모든 쓰레드를 깨운 뒤 자신을 재귀 호출한다.


 * 탐색이 끝난 뒤 삭제 명령 프롬프트를 수행한다. 
 * `getopt` 함수를 이용하여 옵션을 검사하고 삭제/휴지통으로 이동 작업을 수행한다. 
 * 작업이 성공적으로 수행되면 `wlog`를 호출하여 로그를 남긴다. 
 * 휴지통으로 파일을 이동시키는 작업은 설계과제 2에서와 다르게 동작한다. 
 * 아래의 그림은 동작 흐름을 나타낸 것이다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image04.png?raw=true" title="image04.png" alt="image04.png"></img><br/>
</center>

 * 휴지통 파일 정보는 메인 메모리의 `global_trash_list`에 저장된다.
 * 파일 정보가 추가되거나 삭제될 때마다 `save_trash_list`를 호출하여 변경 내용을 저장한다. 
 * 휴지통으로 파일을 옮길 경우 원래 파일 이름과 다르게 파일 이름을 설정한다. 
   * 파일 이름은 휴지통 내에서 해쉬가 유일한 파일인 경우에는 해쉬값, 이미 동일한 해쉬값을 가진 파일이 존재하면 해쉬+정수값으로 설정된다. 
   * 단, 파일 크기가 0인 경우에는 해쉬가 없으므로 해쉬 대신 `ZEROFILE` 문자열로 대체한다. 
   * 정수값은 파일 이름이 유일하게 될 때까지 증가한다.

---
### list 명령

* `list` 명령은 사용자가 입력한 정렬 조건에 따라 현재의 파일 관리 링크드리스트를 정렬하고 중복 파일이 존재하는 파일 리스트들을 순차적으로 출력한다.
* `list -l [LIST_TYPE] -c [CATEGORY] -o [ORDER]` 형식으로 입력하고 각 옵션을 입력하지 않았을 경우 기본값이 세팅된다. 
* `[LIST_TYPE]`과 `[CATEGORY]`의 조합에 따라 수행해야 하는 정렬 작업이 모호한 것이 있었다. 그래서 이 프로그램에서는 아래와 같이 동작하도록 하였다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/table01.png?raw=true" title="table01.png" alt="table01.png"></img><br/>
</center>

---
### trash 명령

 * `trash`는 휴지통에 존재하는 파일 정보를 정렬하여 출력하는 명령이다. 
 * `global_trash_list`에 있는 파일을 주어진 옵션에 따라 정렬하고, 순차적으로 출력한다. 
 * 입력된 정렬 카테고리 옵션이 없다면 파일 이름을 기준으로 정렬하고 정렬 순서는 입력하지 않으면 오름차순이다. 
 * 프로그램 초기에 `trash` 정보 파일을 디스크에서 읽어오므로 프로그램이 종료 후에 실행되어도 이상 없이 동작한다. 
 * `trash`로 출력되는 파일 정보마다 인덱스가 부여되는데 이는 `restore`의 인자로 사용된다.

---
### restore 명령
 `restore` 명령은 `global_trash_list`로부터 복원할 파일을 선택하여 복원하는 명령이다. `trash` 명령을 통해 복원할 파일의 인덱스를 파악할 수 있다. 이 인덱스는 `restore`의 인자가 된다. `restore`의 흐름은 아래의 그림과 같다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image05.png?raw=true" title="image05.png" alt="image05.png"></img><br/>
</center>


 1. 휴지통 파일 정보에는 휴지통 내부에서의 파일 이름과 원래 파일이 존재했던 절대경로가 포함된다. 이 정보를 이용하여 선택된 파일을 휴지통에서 원래의 경로로 옮긴다. 
 2. 만일 원래의 경로에 이미 파일이 있다면 파일 이름 뒤에 정수값을 붙인다. 
 3. 그 이후 전역 `CheckerData` 변수를 이용하여 전역 `fileset`에 파일 정보를 삽입할지 결정한다. `fsha1`이 한번도 수행되지 않았다면 이 작업은 건너뛴다. 
 4. 이후 `wlog`로 로그를 출력하고, `global_trash_list`에서 휴지통 파일 정보를 삭제하기 위해 `delete_trashnode`를 호출한 뒤 변경된 휴지통 정보를 파일로 저장한다.

실행 결과
---
   
<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image06.png?raw=true" title="image06.png" alt="image06.png"></img><br/>
</center>

`make`를 입력하여 컴파일을 하였다. `ssu_sfinder`이 생성되었다.

#### 일반유저 `sjh`로 실행한 경우
<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image07.png?raw=true" title="image07.png" alt="image07.png"></img><br/>
</center>

 쓰레드 개수를 5로 지정하여 중복 파일을 탐색하였다.

 1번 세트의 2번 파일을 삭제하기 위해 `delete –l 1 –d 2`를 입력하였다.

 
<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image08.png?raw=true" title="image08.png" alt="image08.png"></img><br/>
</center>

이후에 `/home/sjh/.duplicate_20172644.log` 파일의 내용을 확인하였다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image09.png?raw=true" title="image09.png" alt="image09.png"></img><br/>
</center>

정상적으로 수행되었다.

`delete –l 5 –i` 를 수행하였다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image10.png?raw=true" title="image10.png" alt="image10.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image11.png?raw=true" title="image11.png" alt="image11.png"></img><br/>
</center>

정상적으로 수행되었다.

`delete –l 1 –t`를 입력하여 휴지통에 파일을 넣어보겠다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image12.png?raw=true" title="image12.png" alt="image12.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image13.png?raw=true" title="image13.png" alt="image13.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image14.png?raw=true" title="image14.png" alt="image14.png"></img><br/>
</center>

정상적으로 수행되었다.

마지막으로 `delete –l 5 –f`를 입력하였다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image15.png?raw=true" title="image15.png" alt="image15.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image16.png?raw=true" title="image16.png" alt="image16.png"></img><br/>
</center>

성공적으로 수행되었다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image17.png?raw=true" title="image17.png" alt="image17.png"></img><br/>
</center>

`list` 명령어와 `trash` 명령어를 수행하였다. 

`restore 2`를 입력해보겠다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image18.png?raw=true" title="image18.png" alt="image18.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image19.png?raw=true" title="image19.png" alt="image19.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image20.png?raw=true" title="image20.png" alt="image20.png"></img><br/>
</center>

성공적으로 파일이 복원되었다.

프로그램을 종료한 뒤에 다시 `trash`를 입력하여도 쓰레기통 정보가 유효하다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image21.png?raw=true" title="image21.png" alt="image21.png"></img><br/>
</center>

#### `root`로 실행한 경우
 로그를 남기기 위해 여러 작업을 수행하였다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image27.png?raw=true" title="image27.png" alt="image27.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image28.png?raw=true" title="image28.png" alt="image28.png"></img><br/>
</center>

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image29.png?raw=true" title="image29.png" alt="image29.png"></img><br/>
</center>

삭제 작업에 대해 로그가 잘 기록된다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image30.png?raw=true" title="image30.png" alt="image30.png"></img><br/>
</center>

휴지통에 파일과 정보가 잘 저장된다.
<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image31.png?raw=true" title="image31.png" alt="image31.png"></img><br/>
</center>

파일 2개를 각각 복구하였고, fileset에 다시 복원되는 모습이다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image32.png?raw=true" title="image32.png" alt="image32.png"></img><br/>
</center>

로그와 비교했을 때 실제로 파일이 잘 복원되었다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image34.png?raw=true" title="image34.png" alt="image34.png"></img><br/>
</center>

/ 경로부터 쓰레드를 5개로 지정하여 모든 중복 파일을 검색하였다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image35.png?raw=true" title="image35.png" alt="image35.png"></img><br/>
</center>

검색 결과가 잘 출력된다.

<center>
        <img src="https://github.com/simjeehoon/LinuxFileComparer/blob/main/readmeimg/image36.png?raw=true" title="image36.png" alt="image36.png"></img><br/>
</center>

`list` 명령도 잘 수행된다.
