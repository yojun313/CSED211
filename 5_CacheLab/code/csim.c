// 20230024 문요준
#include "cachelab.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int is_valid;    // 유효성 여부
    int tag;         // 태그
    int last_used;   // 최근 사용된 순서 (LRU 관리)
} CacheLine;

CacheLine** cache_system; // 캐시 2차원 배열

// 전역 변수
int misses        = 0;  // 캐시 미스 카운트
int hits          = 0;  // 캐시 히트 카운트
int evictions     = 0;  // Eviction 카운트

int current_order = 0;  // LRU를 위한 최근 접근 순서

// 커맨드 옵션 변수 초기화
int set_bits      = 0;  // s
int num_sets      = 0;  // S
int assoc         = 0;  // E
int block_bits    = 0;  // b
int verbose_flag  = 0;

char* trace_file_path;

void cache_simulator(unsigned long long address);

int main(int argc, char* argv[])
{
    char option;
    
    // 명령어에서 파싱하여 커맨드 옵션 변수에 값 대입
    while ((option = getopt(argc, argv, "s:E:b:t:hv")) != -1)
    {
        switch (option)
        {
            case 'h':
                printf("Usage: ./csim [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n");
                return 0;
            case 'v':
                verbose_flag = 1; // Verbose 출력 ON
                break;
            case 's':
                set_bits = atoi(optarg); // optarg(str) -> optarg(int)
                break;
            case 'E':
                assoc = atoi(optarg);
                break;
            case 'b':
                block_bits = atoi(optarg);
                break;
            case 't':
                trace_file_path = optarg;
                break;
            default:
                return 0;
        }
    }

    num_sets = 1 << set_bits; // 2^s = 세트 개수

    // 캐시 전체(2차원 배열) 동적 할당 - 캐시의 세트 수(num_sets)만큼 메모리를 할당하고 각 세트를 가리키는 포인터를 저장할 배열을 생성
    // Ex) 각 세트당 assoc(E)개의 캐시 라인이 있고 캐시 전체에 num_sets만큼의 세트가 있는 경우, CacheLine*은 E개의 CacheLine 배열을 가리키고, CacheLine**은 S개의 포인터 배열을 가리킴
    cache_system = (CacheLine**)malloc(num_sets * sizeof(CacheLine*));
    
    // 캐시 전체 내에서 세트 순회
    for (int i = 0; i < num_sets; i++)
    {
        cache_system[i] = (CacheLine*)malloc(assoc * sizeof(CacheLine)); // 동적 메모리 할당으로 i 세트에 포함할 assoc(E) 개의 라인을 저장
        // 세트 내부의 라인 순회하면서 초기화
        for (int j = 0; j < assoc; j++)
        {
            cache_system[i][j].is_valid  = 0;
            cache_system[i][j].tag       = 0;
            cache_system[i][j].last_used = -1; 
        }
    }

    // Trace 파일 열기
    FILE* trace_file = fopen(trace_file_path, "r");
    if (!trace_file) {
        printf("File is not valid");
        return 0;
    }

    char operation;              // Trace 파일의 명령어를 저장할 변수
    unsigned long long address;  // 메모리 주소를 저장할 변수
    int size;                    // 메모리 접근 시 데이터 크기(byte)를 저장할 변수

    // Trace 파일에서 한 줄씩 데이터를 읽음 --> %c: 명령어 / %llx: 16진수 메모리 주소 / %d: 데이터 크기 / EOF(파일 끝)에 도달할 때까지 루프
    while (fscanf(trace_file, " %c %llx,%d", &operation, &address, &size) != EOF) 
    {
        switch (operation)
        {

            case 'I':                      // 명령어를 메모리에서 읽기 -> 캐시에 영향 X
                break;
            case 'L':                      // Load, 메모리 읽기
                cache_simulator(address);
                break;
            case 'S':                      // Store, 메모리 쓰기
                cache_simulator(address);
                break;
            case 'M':                      // 읽기와 쓰기 둘 다 포함 -> simulator 두 번 호출
                cache_simulator(address);
                cache_simulator(address);
                break;
        }
    }
    fclose(trace_file); // Trace 파일 닫기

     // 메모리 해제
    for (int i = 0; i < num_sets; i++) 
    {
        free(cache_system[i]);
    }
    free(cache_system);

    // 결과 출력
    printSummary(hits, misses, evictions);

    return 0;

}

void cache_simulator(unsigned long long address)
{
    // 메모리 주소 형태: [ Tag | Set Index | Block Offset ]
    int tag       = address >> (set_bits + block_bits);       // (set_bits + block_bits) 만큼 우측 시프트해서 태그 부분 외 하위 비트를 제거, 남은 상위 비트는 태그
    int set_index = (address >> block_bits) & (num_sets - 1); // (block_bits)만큼 우측 시프트해서 Block Offset 제거, (num_sets - 1)를 세트 인덱스를 추출하는데 필요한 비트마스크로 사용, set_bits만 남김

    current_order++;                                          // 최근 접근 순서 증가

    // 캐시 히트 검사, set_index 세트 내에서 라인 순회
    for (int i = 0; i < assoc; i++)
    {
        if (cache_system[set_index][i].is_valid && cache_system[set_index][i].tag == tag) // 유효비트가 1이고 address의 tag와 라인의 tag가 일치할 떄 --> hit!
        {
            hits++;                                                                       // 캐시 히트 카운트 증가
            cache_system[set_index][i].last_used = current_order;                         // LRU를 위한 가장 최근에 사용된 순서를 기록

            if (verbose_flag) printf(" hit");                                             // 캐시 히트 시 결과 출력
            return;
        }
    }

    // 캐시 미스 처리
    misses++;

    // set_index 세트 내에서 라인 순회
    for (int i = 0; i < assoc; i++)
    {
        if (cache_system[set_index][i].is_valid == 0)               // 유효비트가 0인 경우 (라인이 비어있을 경우)
        {
            cache_system[set_index][i].is_valid  = 1;              // 해당 라인을 유효한 상태로 변환
            cache_system[set_index][i].tag       = tag;            // 새 데이터의 태그 저장
            cache_system[set_index][i].last_used = current_order;  // LRU를 위한 가장 최근에 사용된 순서를 기록

            if (verbose_flag) printf(" miss");                     // 캐시 미스 시 결과 출력
            return;
        }
    }

    // Eviction 처리, 캐시의 특정 세트가 다 차있을 떄(캐시 미스 처리 for문에서 모든 is_valid가 1일 때) 데이터 교체를 위한 LRU 구현
    evictions++;
    
    int lru_index = 0;                                        // LRU(가장 오래 사용되지 않은) 라인의 인덱스를 저장하는 변수, 세트의 첫 번째 라인으로 초기화
    int min_order = cache_system[set_index][0].last_used;     // 각 캐시 라인의 last_used 값을 비교해 LRU 데이터 판단을 위한 변수, 첫 번째 라인의 last_used 값으로 초기화
    
    // set_index 세트 내에서 라인 순회
    for (int i = 1; i < assoc; i++)                          
    {
        if (cache_system[set_index][i].last_used < min_order) // 각 라인의 last_used 값을 min_order와 비교한 뒤 더 작으면
        {
            min_order = cache_system[set_index][i].last_used; // 현재 가장 오래된 라인(LRU, last_used가 가장 작은 라인)으로 min_order 값을 업데이트
            lru_index = i;                                    // lru_index 값을 LRU 데이터의 인덱스로 업데이트
        }
    }

    // 교체
    cache_system[set_index][lru_index].tag       = tag;            // LRU 데이터의 tag를 새로운 데이터의 tag로 업데이트
    cache_system[set_index][lru_index].last_used = current_order;  // 최근 사용 순서 갱신
    if (verbose_flag) printf(" miss eviction");                    // miss eviction 시 결과 출력
}

