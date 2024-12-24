/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WORD_SIZE 4                                                                             // Word의 크기(4바이트)를 정의. 헤더 및 풋터의 최소 크기로 사용
#define DOUBLE_WORD_SIZE 8                                                                      // Double Word의 크기(8바이트)를 정의. 블록 크기 정렬과 최소 블록 크기로 사용
#define CHUNK_SIZE (1<<12)                                                                      // 힙 확장을 위한 기본 크기(4KB). 메모리 할당 요청 시 힙을 이만큼 늘림
#define MAX_VALUE(x,y) ((x) > (y) ? (x) : (y))                                                  // 두 값 x, y 중 더 큰 값을 반환. 메모리 관리 시 크기 비교에 사용
#define PACK_BLOCK(size,alloc) ((size) | (alloc))                                               // 블록 크기와 할당 상태를 하나의 값으로 결합
#define GET_WORD(p) (*(unsigned int*)(p))                                                       // 포인터 p가 가리키는 메모리 주소에서 4바이트 값을 읽어옴
#define PUT_WORD(p,val) (*(unsigned int*)(p) = (val))                                           // 포인터 p가 가리키는 메모리 주소에 4바이트 값을 씀
#define GET_BLOCK_SIZE(p) (GET_WORD(p) & ~0x7)                                                  // 포인터 p가 가리키는 헤더 또는 풋터에서 블록 크기를 읽음 / 하위 3비트(~0x7)를 제거하여 순수한 블록 크기만 가져옴
#define GET_ALLOC_STATUS(p) (GET_WORD(p) & 0x1)                                                 // 포인터 p가 가리키는 헤더 또는 풋터에서 할당 상태(0 또는 1)를 읽음
#define HEADER_PTR(bp) ((char*)(bp) - WORD_SIZE)                                                // 블록 포인터(bp)에서 헤더의 시작 주소를 계산
#define FOOTER_PTR(bp) ((char*)(bp) + GET_BLOCK_SIZE(HEADER_PTR(bp)) - DOUBLE_WORD_SIZE)        // 블록 포인터(bp)와 헤더 크기, 블록 크기를 이용하여 풋터의 시작 주소를 계산
#define NEXT_BLOCK_PTR(bp) ((char*)(bp) + GET_BLOCK_SIZE(((char*)(bp) - WORD_SIZE)))            // 블록 포인터(bp)에서 현재 블록의 크기를 더해 다음 블록의 시작 주소를 계산
#define PREVIOUS_BLOCK_PTR(bp) ((char*)(bp) - GET_BLOCK_SIZE(((char*)(bp) - DOUBLE_WORD_SIZE))) // 블록 포인터(bp)에서 이전 블록의 크기를 빼서 이전 블록의 시작 주소를 계산

// Pointer
static char *heap_p; // heap의 시작주소 저장
static char *next_p; // Next Fit 탐색에서 블록 탐색을 시작할 위치를 저장

// define functions
static void *heap_extender(size_t size);
static void *coalescer(void* bp);
static void *fit_finder(size_t size);
static void placer(void *bp, size_t asize);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{  
    // 힙을 초기화하고 초기 메모리를 요청한다.
    void *initial_heap = mem_sbrk(4 * WORD_SIZE);
    if (initial_heap == (void *)-1) 
    { 
        // 메모리 요청 실패 시 -1 반환
        return -1;
    }

    heap_p = initial_heap; // 힙의 시작 주소 설정

    // 정렬 패딩(Alignment Padding)
    // 첫 번째 워드는 정렬을 위해 비워둔다.
    PUT_WORD(heap_p, 0);

    // Prologue Header: 더블 워드 크기(8바이트) 블록, 할당 상태로 설정
    PUT_WORD(heap_p + (1 * WORD_SIZE), PACK_BLOCK(DOUBLE_WORD_SIZE, 1));

    // Prologue Footer: Header와 동일한 설정
    PUT_WORD(heap_p + (2 * WORD_SIZE), PACK_BLOCK(DOUBLE_WORD_SIZE, 1));

    // Epilogue Header: 크기 0, 할당 상태로 설정
    PUT_WORD(heap_p + (3 * WORD_SIZE), PACK_BLOCK(0, 1));

    // 힙 포인터를 프롤로그 블록의 끝(헤더와 풋터 사이)으로 이동
    heap_p += (2 * WORD_SIZE);

    // next_p는 탐색을 시작할 위치를 설정하기 위해 초기화
    next_p = heap_p;

    // 초기 힙 크기를 확장하여 사용할 수 있는 메모리 공간을 추가
    if (heap_extender(CHUNK_SIZE / WORD_SIZE) == NULL) 
    {
        // 힙 확장이 실패하면 -1 반환
        return -1;
    }

    // 초기화 성공 시 0 반환
    return 0;
}


/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    // 예외 처리: 요청된 크기가 0이면 NULL 반환
    if (size == 0) 
    {
        return NULL;
    }

    // 요청된 크기에 따라 조정된 블록 크기를 계산
    size_t adjusted_size;
    if (size <= DOUBLE_WORD_SIZE) 
    {
        // 요청 크기가 최소 블록 크기 이하일 경우, 최소 블록 크기로 조정
        adjusted_size = 2 * DOUBLE_WORD_SIZE;
    } 
    else 
    {
        // 요청 크기 + 헤더/풋터 크기를 더하고, 8바이트 정렬
        adjusted_size = ALIGN(size + DOUBLE_WORD_SIZE);
    }

    // 조정된 크기를 만족하는 적합한 블록을 찾기
    char *block_pointer = fit_finder(adjusted_size);

    if (block_pointer == NULL) 
    {
        // 적합한 블록이 없으면 힙을 확장하여 새 블록 할당
        block_pointer = heap_extender(adjusted_size / WORD_SIZE);

        // 힙 확장이 실패하면 NULL 반환
        if (block_pointer == NULL) 
        {
            return NULL;
        }
    }

    // 찾은 블록 또는 확장된 블록에 요청 크기를 배치
    placer(block_pointer, adjusted_size);

    // 사용 가능한 블록의 시작 주소 반환
    return block_pointer;
}


/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    // 예외 처리: 유효하지 않은 포인터는 처리하지 않음
    if (ptr == NULL) 
    {
        return;
    }

    // 현재 블록의 크기를 가져옴
    size_t size = GET_BLOCK_SIZE(HEADER_PTR(ptr));

    // 블록의 헤더와 풋터를 free 상태로 설정
    PUT_WORD(HEADER_PTR(ptr), PACK_BLOCK(size, 0)); // 헤더에 크기와 할당 상태 저장
    PUT_WORD(FOOTER_PTR(ptr), PACK_BLOCK(size, 0)); // 풋터에 크기와 할당 상태 저장

    // 현재 블록을 병합하여 단편화를 줄임
    coalescer(ptr);
}


/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    // 기존 블록의 포인터 저장
    void *old_ptr = ptr;

    // 새로운 블록을 저장할 포인터 선언
    void *new_ptr;

    // 새롭게 필요한 크기를 계산 (요청 크기 + 헤더/풋터 크기)
    size_t new_size = size + DOUBLE_WORD_SIZE;

    // 기존 블록의 크기를 가져옴
    size_t old_size = GET_BLOCK_SIZE(HEADER_PTR(old_ptr));

    // 현재 블록을 포함한 누적 크기 초기화
    size_t total_size = old_size;

    // 블록 병합을 위한 플래그
    int found_sufficient_space = 0;
    int wrap_around_flag = 0;

    // 1. 요청 크기가 기존 블록 크기 이하라면, 기존 블록을 그대로 사용
    if (new_size <= old_size) 
    {
        return old_ptr;
    }

    // 2. 병합 가능한 연속된 free 블록을 검색
    void *temp_ptr = old_ptr;
    while (GET_ALLOC_STATUS(HEADER_PTR(NEXT_BLOCK_PTR(temp_ptr))) == 0) 
    {
        // 다음 블록의 크기를 누적
        total_size += GET_BLOCK_SIZE(HEADER_PTR(NEXT_BLOCK_PTR(temp_ptr)));

        // 탐색 중 `next_p`를 초과했는지 확인
        if (temp_ptr == next_p)
        {
            wrap_around_flag = 1;
        }

        // 누적 크기가 요청 크기를 충족하면 탐색 종료
        if (new_size <= total_size) 
        {
            found_sufficient_space = 1;
            break;
        }

        // 다음 블록으로 이동
        temp_ptr = NEXT_BLOCK_PTR(temp_ptr);
    }

    // 3. 충분한 크기의 연속된 블록을 찾았을 경우
    if (found_sufficient_space) 
    {
        // 기존 블록을 확장하여 헤더와 풋터를 업데이트
        PUT_WORD(HEADER_PTR(old_ptr), PACK_BLOCK(total_size, 1)); // 헤더 갱신
        PUT_WORD(FOOTER_PTR(old_ptr), PACK_BLOCK(total_size, 1)); // 풋터 갱신

        // 탐색 중 `next_p`를 초과했으면 `next_p`를 갱신
        if (wrap_around_flag) 
        {
            next_p = old_ptr;
        }

        // 기존 블록 확장 후 반환
        return old_ptr;
    }

    // 4. 충분한 크기의 블록을 찾지 못한 경우
    // 새 블록을 할당
    new_ptr = mm_malloc(new_size);

    // 할당 실패 시 NULL 반환
    if (new_ptr == NULL) 
    {
        return NULL;
    }

    // 5. 기존 블록의 데이터를 새 블록으로 복사
    memcpy(new_ptr, old_ptr, old_size);

    // 기존 블록을 free
    mm_free(old_ptr);

    // 새 블록의 포인터 반환
    return new_ptr;
}


static void *heap_extender(size_t words)
{
    // 확장할 힙 영역의 시작 포인터와 크기를 선언
    char *bp;
    size_t size;

    // 요청된 word 수가 짝수인지 확인하여 size 계산
    if (words % 2 == 0) 
    {
        size = WORD_SIZE * words; // 짝수일 경우 그대로 계산
    } else 
    {
        size = WORD_SIZE * (words + 1); // 홀수일 경우 한 word 추가
    }

    // 힙을 요청된 크기만큼 확장
    bp = mem_sbrk(size);

    // 힙 확장이 실패한 경우 NULL 반환
    if (bp == (void *)-1) 
    {
        return NULL;
    }

    // 새로 확장된 블록의 헤더와 풋터를 설정 (free 상태)
    PUT_WORD(HEADER_PTR(bp), PACK_BLOCK(size, 0));  // 헤더 설정
    PUT_WORD(FOOTER_PTR(bp), PACK_BLOCK(size, 0));  // 풋터 설정

    // 새로운 에필로그 헤더를 생성 (할당된 상태, 크기 0)
    PUT_WORD(HEADER_PTR(NEXT_BLOCK_PTR(bp)), PACK_BLOCK(0, 1));

    // 새로 확장된 블록을 병합하고 병합된 블록의 시작 포인터를 반환
    return coalescer(bp);
}


static void *coalescer(void *bp)
{
    // 이전 블록과 다음 블록의 할당 상태를 확인
    size_t prev_alloc = GET_ALLOC_STATUS(FOOTER_PTR(PREVIOUS_BLOCK_PTR(bp)));
    size_t next_alloc = GET_ALLOC_STATUS(HEADER_PTR(NEXT_BLOCK_PTR(bp)));

    // 현재 블록의 크기를 가져옴
    size_t size = GET_BLOCK_SIZE(HEADER_PTR(bp));

    // Case 1: 이전 블록과 다음 블록 모두 할당된 상태
    if (prev_alloc && next_alloc) 
    {
        next_p = bp;  // `next_p`를 현재 블록으로 설정
        return bp;    // 병합할 필요가 없으므로 그대로 반환
    }

    // Case 2: 이전 블록은 할당되고 다음 블록은 free 상태
    if (prev_alloc && !next_alloc) 
    {
        size += GET_BLOCK_SIZE(HEADER_PTR(NEXT_BLOCK_PTR(bp))); // 다음 블록 크기 추가
        PUT_WORD(HEADER_PTR(bp), PACK_BLOCK(size, 0));         // 헤더 갱신
        PUT_WORD(FOOTER_PTR(bp), PACK_BLOCK(size, 0));         // 풋터 갱신
    }

    // Case 3: 이전 블록은 free 상태이고 다음 블록은 할당된 상태
    else if (!prev_alloc && next_alloc) 
    {
        size += GET_BLOCK_SIZE(HEADER_PTR(PREVIOUS_BLOCK_PTR(bp))); // 이전 블록 크기 추가
        PUT_WORD(FOOTER_PTR(bp), PACK_BLOCK(size, 0));             // 풋터 갱신
        PUT_WORD(HEADER_PTR(PREVIOUS_BLOCK_PTR(bp)), PACK_BLOCK(size, 0)); // 이전 블록 헤더 갱신
        bp = PREVIOUS_BLOCK_PTR(bp);                               // 블록 포인터를 이전 블록으로 이동
    }

    // Case 4: 이전 블록과 다음 블록 모두 free 상태
    else 
    {
        size += GET_BLOCK_SIZE(HEADER_PTR(PREVIOUS_BLOCK_PTR(bp))) +
                GET_BLOCK_SIZE(FOOTER_PTR(NEXT_BLOCK_PTR(bp))); // 이전 및 다음 블록 크기 추가
        PUT_WORD(HEADER_PTR(PREVIOUS_BLOCK_PTR(bp)), PACK_BLOCK(size, 0)); // 이전 블록 헤더 갱신
        PUT_WORD(FOOTER_PTR(NEXT_BLOCK_PTR(bp)), PACK_BLOCK(size, 0));     // 다음 블록 풋터 갱신
        bp = PREVIOUS_BLOCK_PTR(bp);                                       // 블록 포인터를 이전 블록으로 이동
    }

    // `next_p`를 병합된 블록의 시작 포인터로 업데이트
    next_p = bp;

    // 병합된 블록의 시작 포인터 반환
    return bp;
}


static void *fit_finder(size_t size)
{
    // 힙을 순회하기 위한 포인터
    void *bp;

    // 적합한 블록을 찾았을 경우 해당 블록의 포인터를 저장할 변수
    void *next_fit_block = NULL;

    // next_p가 NULL일 경우 힙의 시작 지점으로 초기화
    if (next_p == NULL) 
    {
        next_p = heap_p;
    }

    // 힙의 끝까지 순회하며 적합한 free 블록을 찾음
    bp = next_p;
    while (GET_BLOCK_SIZE(HEADER_PTR(bp)) > 0) 
    {
        // 현재 블록이 할당된 상태면 다음 블록으로 이동
        if (GET_ALLOC_STATUS(HEADER_PTR(bp))) 
        {
            bp = NEXT_BLOCK_PTR(bp);
            continue;
        }

        // 요청된 크기보다 크거나 같은 free 블록을 찾았을 경우
        if (size <= GET_BLOCK_SIZE(HEADER_PTR(bp))) 
        {
            next_fit_block = bp; // 해당 블록을 적합한 블록으로 설정
            next_p = bp;         // next_p를 현재 블록으로 갱신
            break;               // 탐색 종료
        }

        // 다음 블록으로 이동
        bp = NEXT_BLOCK_PTR(bp);
    }

    // 적합한 블록의 포인터를 반환 (없으면 NULL 반환)
    return next_fit_block;
}

static void placer(void *bp, size_t size)
{
    // 현재 블록의 크기를 가져옴
    size_t current_size = GET_BLOCK_SIZE(HEADER_PTR(bp));

    // Case 1: 블록을 분할할 수 있을 만큼 충분히 큰 경우
    if ((current_size - size) >= (2 * DOUBLE_WORD_SIZE)) 
    {
        // 현재 블록을 요청된 크기로 설정 (헤더와 풋터 갱신)
        PUT_WORD(HEADER_PTR(bp), PACK_BLOCK(size, 1));  // 헤더 설정
        PUT_WORD(FOOTER_PTR(bp), PACK_BLOCK(size, 1));  // 풋터 설정

        // 분할 후 남은 공간을 새로운 free 블록으로 설정
        void *next_bp = NEXT_BLOCK_PTR(bp);             // 다음 블록의 시작 주소 계산
        PUT_WORD(HEADER_PTR(next_bp), PACK_BLOCK(current_size - size, 0)); // 헤더 갱신
        PUT_WORD(FOOTER_PTR(next_bp), PACK_BLOCK(current_size - size, 0)); // 풋터 갱신

        // `next_p`를 새로 생성된 free 블록으로 업데이트
        next_p = next_bp;

    // Case 2: 블록을 분할할 수 없을 만큼 작은 경우
    } else 
    {
        // 현재 블록 전체를 할당 상태로 설정 (헤더와 풋터 갱신)
        PUT_WORD(HEADER_PTR(bp), PACK_BLOCK(current_size, 1)); // 헤더 설정
        PUT_WORD(FOOTER_PTR(bp), PACK_BLOCK(current_size, 1)); // 풋터 설정

        // `next_p`를 현재 블록으로 업데이트
        next_p = bp;
    }
}














