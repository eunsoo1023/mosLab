# mosLab: AI 에이전트 기반 miniOS 구현 실습

이 저장소는 교수자 배포본이다. `include/`, `apps/`, `tests/`, `docs/instructor/`, `scripts/`, `Makefile`, `lib/libmosvm.a`는 고정 API와 검증 기준을 제공한다.

학생은 `src/` 내부 구현과 이 README의 개인 설계 설명만 수정한다. `src/`에는 초기 정답 구현이 없으며, 각 파일의 한국어 주석이 LAB별 구현 목표와 주의사항을 설명한다.

로컬 파일 수정 금지는 보안 경계가 아니다. 채점은 학생 제출물에서 `src/**`와 `README.md`만 추출하고, 신뢰된 교수자 헤더, VM 라이브러리, 테스트와 결합해 수행한다.

## 빠른 시작

```sh
./scripts/verify_lab0.sh
make verify-instructor
make test-lab01
```

## 주요 명령

- `make help`: 학생 배포본에서 지원하는 명령 확인
- `make verify-instructor`: 공개 헤더, 앱, 테스트 syntax와 배포된 VM 라이브러리 검증
- `make minios`: 학생 `src/` 구현과 VM 라이브러리 링크
- `make demo-lab01` ... `make demo-lab10`: LAB별 콘솔 데모
- `make test-lab01` ... `make test-lab10`: LAB별 공개 자동검증
- `make test`: LAB1-LAB10 공개 테스트 전체 실행
- `make clean`: 빌드 산출물 정리. `lib/libmosvm.a`는 삭제하지 않는다.

## 교수자/CI 전용

학생 배포 Makefile은 VM 라이브러리를 재생성하지 않는다. 교수자 또는 CI가 VM 런타임을 갱신해야 할 때만 다음 명령을 사용한다.

```sh
make -C instructor vm-lib
```

채점 서버는 학생 저장소의 `instructor/`, `include/`, `tests/`, `lib/` 변경본을 사용하지 않는다.

## 개인 설계 설명

학생은 각 LAB을 구현하면서 선택한 자료구조, 오류 처리 정책, 범위 검사 전략을 여기에 기록한다. 공개 API는 `include/minios/`를 변경하지 않는다.

### 공통 설계 원칙

- 공개 함수 시그니처와 자료형은 변경하지 않고 `src/*.c`와 `src/private/` 안에서만 구현했다.
- 여러 subsystem이 함께 사용하는 상태는 `mos_process_table_t`가 가리키는 공통 private state에 보관했다.
- 포인터 인자는 먼저 검사하고, VM 오류는 `mos_status_t`로 변환했다.
- 고정 크기 자원은 범위, 중복 할당, 중복 반납을 검사해 상태를 손상시키지 않는다.
- 실패한 작업은 가능한 한 출력값과 내부 상태를 변경하지 않도록 했다.

### LAB1: Kernel Lifecycle

`mos_kernel_t` 자체에 공개 lifecycle 상태와 VM handle을 보관했다. `mos_kernel_boot()`은 VM을 생성한 뒤 `MOS_KERNEL_BOOTED`로 전환하고, `mos_kernel_shutdown()`은 VM과 private state를 해제한 뒤 `MOS_KERNEL_SHUTDOWN`으로 전환한다. 부팅 전 tick/조회와 중복 boot는 `MOS_ERR_STATE`를 반환한다.

공개 테스트가 초기화되지 않은 지역 `mos_kernel_t`에 boot를 호출하므로 boot 진입 시 구조체를 0으로 초기화한다. VM 오류는 NULL, 범위, 상태, 자원 부족에 맞춰 `MOS_ERR_NULL`, `MOS_ERR_RANGE`, `MOS_ERR_STATE`, `MOS_ERR_NO_SPACE`로 변환한다.

### LAB2: Process

커널별 고정 배열 `MOS_PROCESS_MAX`개의 PCB를 사용한다. PCB에는 PID, process state, `vm_cpu_context_t`가 들어 있고 process table에는 현재 count와 다음 PID가 있다. PID는 1부터 순차 발급하며 0은 유효한 PID로 사용하지 않는다.

프로세스 생성은 빈 슬롯과 PID를 확인한 뒤 `vm_cpu_context_init()`이 성공한 경우에만 PCB를 기록한다. 새 프로세스는 `MOS_PROC_READY`로 시작한다. PID 조회 실패는 `MOS_ERR_NOT_FOUND`, 잘못된 PID는 `MOS_ERR_RANGE`, 테이블 초과는 `MOS_ERR_NO_SPACE`다.

### LAB3: Round-Robin Scheduler

고정 크기 원형 배열을 ready queue로 사용한다. queue는 head와 size로 관리하며 `mos_scheduler_next()`는 PID를 제거하지 않고 head 항목을 tail로 회전시켜 RR 순서를 제공한다.

CPU context는 PCB에 저장하고 scheduler는 최대 `time_slice_ticks`만큼 `vm_cpu_step()`을 호출한다. `YIELD`와 time slice 만료는 `RUNNING -> READY`, `HALT`는 `RUNNING -> EXITED`, `TRAP`은 LAB3에서 `READY` 복귀로 처리한다. 빈 queue는 `MOS_ERR_NOT_FOUND`, 중복 enqueue와 잘못된 상태는 `MOS_ERR_STATE`다.

### LAB4: Synchronization

semaphore와 mutex는 각각 FIFO 원형 waiter queue를 private state로 가진다. semaphore 값이 양수면 wait가 값을 감소시키고 성공하며, 값이 0이면 PID를 queue에 넣고 PCB를 `BLOCKED`로 바꾼 뒤 `MOS_ERR_BLOCKED`를 반환한다. signal은 waiter를 FIFO 순서로 `READY`로 깨우고, waiter가 없을 때만 값을 증가시킨다.

mutex는 owner PID를 기록한다. 첫 lock은 owner를 설정하고, 다른 PID의 lock은 waiter queue에 넣어 `BLOCKED`를 반환한다. owner만 unlock할 수 있으며 waiter가 있으면 잠금을 해제하는 대신 다음 waiter에게 owner를 직접 이전한다. 재귀 lock과 비소유자 unlock은 `MOS_ERR_STATE`다.

### LAB5: Physical Memory

VM의 `frame_count`를 읽어 frame 번호와 일대일 대응하는 bitmap allocator를 구성했다. bitmap bit가 0이면 free, 1이면 allocated이며 `free_frames`는 bitmap의 free bit 수와 일치해야 한다.

할당은 첫 free frame을 찾아 bit를 설정하고 free count를 감소시킨다. 반납은 범위와 현재 할당 여부를 검사한 뒤 bit를 해제한다. 범위 밖 frame은 `MOS_ERR_RANGE`, 이미 free인 frame은 `MOS_ERR_STATE`, free frame 부족은 `MOS_ERR_NO_SPACE`다.

### LAB6: Virtual Memory

page table은 `(pid, virtual_page)`를 key로 하는 private 연결 리스트다. PID가 다르면 같은 virtual page도 별도의 mapping을 가질 수 있다. mapping에는 frame과 writable bit를 저장한다.

가상 주소는 다음처럼 page와 offset으로 분해한다.

```text
virtual_page = virtual_address / MOS_VM_PAGE_SIZE
offset       = virtual_address % MOS_VM_PAGE_SIZE
physical     = frame * MOS_VM_PAGE_SIZE + offset
```

할당되지 않은 frame, 존재하지 않는 PID, 중복 mapping은 각각 범위/상태/존재 오류로 처리한다. unmap은 page table entry만 삭제하고 frame을 자동 반납하지 않는다. 매핑이 없는 page는 `MOS_ERR_NOT_FOUND`다.

### LAB7: Block Device and File System

block device layer는 VM raw block API를 감싸 block size/count, index, buffer 크기를 검증한다. 파일시스템은 고정 layout을 사용한다.

```text
block 0       superblock
block 1..4    inode table
block 5..63   file data blocks
```

inode private metadata에는 used flag, inode 번호, path, byte 단위 file size, data block 목록을 저장한다. path는 absolute path이며 빈 path, trailing slash, 연속 slash, `.`, `..`, 최대 길이 초과를 거부한다.

파일 생성은 빈 inode를 할당하고, write는 필요한 data block을 확보해 내용을 기록한 뒤 size와 block count를 갱신한다. read는 buffer 크기만큼 partial read를 허용하고 실제 byte 수를 `read_out`에 기록한다. 없는 파일은 `MOS_ERR_NOT_FOUND`, 중복 생성은 `MOS_ERR_STATE`, 공간 부족은 `MOS_ERR_NO_SPACE`다.

### LAB8: System Calls and FD Table

커널별 16개 FD slot을 사용하며 FD 값은 slot index와 같다. slot은 used flag, 연결된 path, offset을 가진다. open은 기존 파일만 열고 가장 작은 free slot을 반환한다. close는 FD slot만 해제하며 inode와 파일 데이터는 유지한다.

음수, 범위 밖, 이미 닫힌 FD는 모두 `MOS_ERR_INVALID`다. write/read의 출력 크기 포인터는 호출 초기에 0으로 설정하고, 성공한 경우에만 실제 처리 크기를 기록한다. 현재 FS API는 path 기반이므로 FD offset은 다음 확장 지점을 위해 저장만 한다.

### LAB9: Shell

shell은 private initialized/exited 상태를 가지며, 현재 지원 명령은 인자 없는 `help`와 `exit`다. parser는 앞뒤 whitespace와 연속 whitespace를 처리하고 빈 줄은 성공으로 처리한다. 알 수 없는 command는 `MOS_ERR_NOT_FOUND`, 잘못된 인자는 `MOS_ERR_INVALID`, 너무 긴 줄은 `MOS_ERR_RANGE`다.

출력은 VM console API를 통해 기록한다. script 실행 전 console을 clear하고, `exit`를 만나면 이후 입력을 중단한다. 마지막에는 `vm_console_snapshot()`을 사용하므로 output은 항상 NUL 종료되어야 하며 buffer가 작거나 console이 가득 차면 각각 `MOS_ERR_INVALID` 또는 `MOS_ERR_NO_SPACE`를 반환한다.

### LAB10: Integration

통합 boot 순서는 다음과 같다.

```text
kernel
 -> process table
 -> scheduler
 -> sync readiness
 -> memory
 -> virtual memory
 -> block device + file system
 -> syscall
 -> shell
```

`labs_ready`는 각 단계가 성공한 뒤에만 증가한다. 실패한 단계는 count에 포함하지 않고 최초 오류 status를 report에 기록한다. 실패 시 kernel shutdown으로 private 자원을 정리하며 cleanup 오류가 원래 실패 status를 덮어쓰지 않게 한다.

통합 demo는 public API만 사용해 파일 생성, open/write/close, shell script를 실행한다. block device는 FS보다 오래 살아야 하므로 공통 private state에 저장해 FS가 지역 변수 주소를 참조하지 않도록 했다.

### 검증 결과

다음 명령으로 LAB1부터 LAB10까지 공개 테스트와 엄격한 C99 검사를 실행했다.

```sh
make clean
make test
make syntax-check
```

모든 공개 LAB 테스트가 통과했고 `make syntax-check`도 통과했다.
