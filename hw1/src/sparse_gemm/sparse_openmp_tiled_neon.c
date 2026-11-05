/*
 * ============================================================================
 *  sparse_openmp_tiled_neon.c  --  kernel COMBINATO (Esercizio 5)
 *                                  tile di 4 righe + NEON + OpenMP
 *                                  + LOOP UNROLLING solo nella parte dei PESI
 * ============================================================================
 *
 * OBIETTIVO
 *   Calcolare C = A * W (W sparsa a blocchi 4x4, formato BCC) mettendo insieme
 *   le ottimizzazioni dei kernel precedenti:
 *     1) TILING di 4 righe  (sparse_tiled.c)  -> ogni blocco di pesi, una volta
 *        letto, viene riusato per 4 righe di A;
 *     2) NEON               (sparse_neon.c)   -> le 4 colonne adiacenti di un
 *        gruppo vengono aggiornate con una sola istruzione SIMD;
 *     3) OPENMP             (sparse_openmp.c) -> i tile di righe, che sono
 *        indipendenti, vengono distribuiti tra i thread;
 *     4) LOOP UNROLLING     -> solo il ciclo sulle 4 righe locali del blocco
 *        ("r"), cioe' la parte dei pesi, e' scritto per esteso.
 *
 * IMPLEMENTAZIONE AD ALTO LIVELLO
 *   - Le righe di A sono raggruppate in "tile" da 4 righe: tile t = righe
 *     4t, 4t+1, 4t+2, 4t+3. Con M = 128 ci sono 32 tile.
 *   - Per ogni tile e per ogni gruppo g di 4 colonne di output si tengono
 *     QUATTRO accumulatori NEON acc[ri] (float32x4_t), uno per riga del tile;
 *     ognuno ha 4 lane = le 4 colonne del gruppo.
 *   - Per ogni blocco memorizzato p si caricano SUBITO le 4 righe di pesi del
 *     blocco (w0..w3, una volta sola: srotolamento del ciclo r) e si riusano
 *     per tutte le righe del tile.
 *   - Il ciclo sulle righe del tile ("ri") NON e' srotolato: resta un normale
 *     ciclo, come gli altri cicli esterni (tile, gruppo, blocco).
 *   - Il ciclo ESTERNO sui tile e' parallelizzato con un semplice
 *     '#pragma omp parallel for' (senza collapse/schedule/reduction).
 *     Ogni tile scrive solo le sue 4 righe di C, quindi non ci sono conflitti.
 *   - Se m non e' multiplo di 4, le righe rimanenti sono calcolate dopo il
 *     ciclo parallelo con il percorso NEON a una riga (con M = 128 non serve,
 *     ma rende il kernel corretto in generale).
 *   L'ordine delle somme su ogni accumulatore e' r = 0,1,2,3, come negli altri
 *   kernel.
 * ============================================================================
 */

#include "sparse_gemm.h"   /* tipo sparse_weights, codici di ritorno, prototipi */
#include <arm_neon.h>      /* intrinsics NEON: float32x4_t, vld1q_f32, vfmaq_f32, ... */

/* Righe elaborate insieme nel tile (fissato a 4 dalla consegna). */
#define TM 4

int gemm_sparse_openmp_tiled_neon(const float *a, const sparse_weights *w,
                                  float *c, size_t m)
{
    /* Controllo difensivo sui puntatori. */
    if (!a || !w || !c)
        return SPARSE_ERROR;

    /* Numero di gruppi di 4 colonne di output: N / 4 (es. 512/4 = 128). */
    const size_t groups = w->n / 4;

    /* Numero di tile COMPLETI da 4 righe: m / 4 (es. 128/4 = 32). */
    const size_t n_tiles = m / TM;

    /* ---------- Parte principale: tile completi, in parallelo ---------- */

    /* Parallelizzo SOLO il ciclo sui tile: ogni iterazione e' indipendente e
       scrive righe di C distinte. Pragma semplice, senza clausole. La guardia
       #ifdef evita problemi se si compila senza -fopenmp. */
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (size_t t = 0; t < n_tiles; ++t) {

        /* Indice della prima riga del tile t (variabile privata al thread). */
        const size_t i0 = t * TM;

        /* Ciclo sui gruppi di 4 colonne di output. */
        for (size_t g = 0; g < groups; ++g) {

            /* 4 accumulatori NEON, uno per riga del tile. Ogni accumulatore ha
               4 lane = le colonne 4g..4g+3. Tutti partono da zero. */
            float32x4_t acc[TM];
            
            acc[0] = vdupq_n_f32(0.0f);
            acc[1] = vdupq_n_f32(0.0f);
            acc[2] = vdupq_n_f32(0.0f);
            acc[3] = vdupq_n_f32(0.0f);

            /* Blocchi memorizzati del gruppo g: p in [col_ptr[g], col_ptr[g+1]).
               I blocchi mancanti (tutti zero) non compaiono e sono saltati. */
            for (size_t p = w->block_col_ptr[g]; p < w->block_col_ptr[g + 1]; ++p) {

                /* Primo indice di riduzione del blocco: k0 = 4 * blocco-riga. */
                const size_t k0 = 4 * (size_t)w->block_row_idx[p];

                /* Puntatore ai 16 pesi del blocco p (row-major 4x4). */
                const float *blk = w->block_values + 16 * p;

                /* Carico le 4 righe di pesi del blocco UNA volta sola (ciclo r
                   srotolato): restano nei registri NEON e servono per tutte le
                   righe del tile. w0 = riga locale 0, w1 = riga 1, ... */
                const float32x4_t w0 = vld1q_f32(blk + 0);
                const float32x4_t w1 = vld1q_f32(blk + 4);
                const float32x4_t w2 = vld1q_f32(blk + 8);
                const float32x4_t w3 = vld1q_f32(blk + 12);

                /* Ciclo sulle 4 righe del tile (NON srotolato): riuso dei pesi. */
                for (size_t ri = 0; ri < TM; ++ri) {

                    /* Puntatore ai 4 valori di input della riga (i0+ri) del tile:
                       x[r] = A[i0+ri, k0+r], per r = 0..3. */
                    const float *x = a + (i0 + ri) * w->k + k0;

                    /* Una FMA per riga locale: broadcast di x[r] su 4 lane e
                       acc[ri] += x[r] * w_r (le 4 colonne insieme). */
                    acc[ri] = vfmaq_f32(acc[ri], vdupq_n_f32(x[0]), w0);  /* r = 0 */
                    acc[ri] = vfmaq_f32(acc[ri], vdupq_n_f32(x[1]), w1);  /* r = 1 */
                    acc[ri] = vfmaq_f32(acc[ri], vdupq_n_f32(x[2]), w2);  /* r = 2 */
                    acc[ri] = vfmaq_f32(acc[ri], vdupq_n_f32(x[3]), w3);  /* r = 3 */
                }
            }

            /* Scrivo i 4 risultati di ogni riga del tile nelle colonne
               4g..4g+3 di C (una scrittura vettoriale per riga). */
            for (size_t ri = 0; ri < TM; ++ri)
                vst1q_f32(c + (i0 + ri) * w->n + 4 * g, acc[ri]);
        }
    }

    /* ---------- Residuo: righe rimanenti se m non e' multiplo di 4 ---------- */

    /* Prima riga non coperta dai tile completi. Eseguito da un solo thread
       (dopo il ciclo parallelo); con M = 128 il ciclo non esegue iterazioni. */
    for (size_t i = n_tiles * TM; i < m; ++i) {
        for (size_t g = 0; g < groups; ++g) {

            /* Percorso NEON a una riga, identico a gemm_sparse_neon. */
            float32x4_t acc = vdupq_n_f32(0.0f);
            for (size_t p = w->block_col_ptr[g]; p < w->block_col_ptr[g + 1]; ++p) {
                const size_t k0 = 4 * (size_t)w->block_row_idx[p];
                const float *blk = w->block_values + 16 * p;
                for (size_t r = 0; r < 4; ++r) {
                    const float32x4_t av = vdupq_n_f32(a[i * w->k + k0 + r]);
                    const float32x4_t wv = vld1q_f32(blk + 4 * r);
                    acc = vfmaq_f32(acc, av, wv);
                }
            }
            vst1q_f32(c + i * w->n + 4 * g, acc);
        }
    }

    /* Kernel completato con successo. */
    return SPARSE_OK;
}
