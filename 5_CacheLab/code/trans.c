/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

void transpose_MxN(int M, int N, int A[N][M], int B[M][N], int block_size);

void transpose_64x64(int A[64][64], int B[64][64]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    if (M == 32 && N == 32) 
    {
        transpose_MxN(M, N, A, B, 8);
    } 
    else if (M == 64 && N == 64) 
    {
        transpose_64x64(A, B);
    } 
    else if (M == 61 && N == 67) 
    {
        transpose_MxN(M, N, A, B, 16);
    }
}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

void transpose_MxN(int M, int N, int A[N][M], int B[M][N], int block_size)
{
    int i, j;                                       // i, j: 현재 처리 중인 블록 시작점,
    int row, col;                                   // row, col: 블록 내 인덱스
    int diag_value, diag_index;                     // 대각선 원소 처리용 변수

    // i, j 순회 --> 한 번의 반복마다 block_size x block_size 블록 처리
    for (i = 0; i < N; i += block_size)
    {
        for (j = 0; j < M; j += block_size)
        {
            // row, col 순회 --> 블록 내부 데이터 순회
            for (row = 0; row < block_size && i + row < N; row++)
            {
                for (col = 0; col < block_size && j + col < M; col++)
                {
                    if (i + row == j + col)                             // 대각선 원소일 때 (열과 행 인덱스가 같을 때)
                    {                                                   // 대각선 원소는 전치 시 동일한 위치에 저장됨 --> 캐시 미스 발생 가능
                        diag_value = A[i + row][j + col];               // diag_value에 대각선 원소의 값 저장
                        diag_index = i + row;                           // diag_index에 대각선 원소의 위치 저장
                    }
                    else
                    {
                        B[j + col][i + row] = A[i + row][j + col];      // 전치된 위치(B 행렬)로 값 복사
                    }
                }
                
                if (i + row == j + row)                                 // 블록 행 내에서 열 순회가 끝나고 대각선 원소가 존재하면 
                {
                    B[diag_index][diag_index] = diag_value;             // 해당 위치에 대각선 원소의 값 저장
                }
            }
        }
    }
}

void transpose_64x64(int A[64][64], int B[64][64]) 
{
    int i, j;                                                   // 현재 처리 중인 블록의 행 및 열 시작점
    int row, col;                                               // 블록 내 행과 열 인덱스
    int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7; // 임시 저장 변수

    // i, j 순회 --> 한 번의 반복마다 8x8 블록 처리
    for (i = 0; i < 64; i += 8) 
    {
        for (j = 0; j < 64; j += 8) 
        {

            // 상단 4x8 영역 처리
            for (row = 0; row < 4; row++) 
            {
                // 행렬 A에서 현재 블록의 상단 4행 데이터를 tmp 배열에 저장
                // temp0~3: A 블록의 좌상단(4x4) 데이터
                // temp4~7: A 블록의 우상단(4x4) 데이터
                temp0 = A[i + row][j + 0];
                temp1 = A[i + row][j + 1];
                temp2 = A[i + row][j + 2];
                temp3 = A[i + row][j + 3];
                temp4 = A[i + row][j + 4];
                temp5 = A[i + row][j + 5];
                temp6 = A[i + row][j + 6];
                temp7 = A[i + row][j + 7];
                
                // tmp 배열의 데이터를 행렬 B에 전치된 위치로 복사
                B[j + 0][i + row] = temp0;                      // A 블록의 좌상단(4x4) 데이터를 전치해서 B 블록의 좌상단에 저장 --> A 블록의 좌상단(4x4)영역 전치 완료
                B[j + 1][i + row] = temp1;
                B[j + 2][i + row] = temp2;
                B[j + 3][i + row] = temp3;

                B[j + 3][i + row + 4] = temp4;                  // A 블록의 우상단(4x4) 데이터를 전치해서 B 블록의 우상단에 저장
                B[j + 2][i + row + 4] = temp5;
                B[j + 1][i + row + 4] = temp6;
                B[j + 0][i + row + 4] = temp7;
            }

            // 하단 4x8 영역 처리
            for (col = 0; col < 4; col++) 
            {
                // A 블록의 하단 4x8 영역 데이터를 tmp 배열에 저장
                // temp0 ~ temp3: A 블록의 좌하단(4x4) 데이터
                // temp4 ~ temp7: A 블록의 우하단(4x4) 데이터
                temp0 = A[i + 4][j + 3 - col];
                temp1 = A[i + 5][j + 3 - col];
                temp2 = A[i + 6][j + 3 - col];
                temp3 = A[i + 7][j + 3 - col];
                temp4 = A[i + 4][j + 4 + col];
                temp5 = A[i + 5][j + 4 + col];
                temp6 = A[i + 6][j + 4 + col];
                temp7 = A[i + 7][j + 4 + col];

                // B 블록의 우상단(4x4) 영역 데이터를 좌하단(4x4) 영역으로 재배치 --> A 블록의 우상단(4x4)영역 전치 완료
                // B[j + 4 + col][i + row] = B[j + 3 - col][i + 4 + row]
                for (row = 0; row < 4; row++) {
                    B[j + 4 + col][i + row] = B[j + 3 - col][i + 4 + row];
                }

                // temp0 ~ temp3에 저장된 A 블록의 좌하단(4x4) 데이터를
                // B 블록의 우상단(4x4) 영역에 전치된 형태로 복사               --> A 블록의 좌하단(4x4)영역 전치 완료
                B[j + 3 - col][i + 4] = temp0;
                B[j + 3 - col][i + 5] = temp1;
                B[j + 3 - col][i + 6] = temp2;
                B[j + 3 - col][i + 7] = temp3;

                // temp4 ~ temp7에 저장된 A 블록의 우하단(4x4) 데이터를
                // B 블록의 우하단(4x4) 영역에 전치된 형태로 복사               --> A 블록의 우하단(4x4)영역 전치 완료
                B[j + 4 + col][i + 4] = temp4;
                B[j + 4 + col][i + 5] = temp5;
                B[j + 4 + col][i + 6] = temp6;
                B[j + 4 + col][i + 7] = temp7;
            }
        }
    }
}


