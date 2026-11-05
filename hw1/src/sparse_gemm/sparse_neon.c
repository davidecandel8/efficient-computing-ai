/*
 * ============================================================================
 *  sparse_neon.c  --  4x4 block-sparse GEMM con NEON SIMD (Esercizio 2)
 *                     + LOOP UNROLLING manuale
 * ============================================================================
 *
 * OBIETTIVO
 *   Calcolare C = A * W, con A (m x K) densa e W (K x N) sparsa a blocchi 4x4
 *   in formato BCC (block_col_ptr / block_row_idx / block_values), usando le
 *   istruzioni NEON di ARMv8 (Cortex-A72 del Raspberry Pi 4).
 *
 * IMPLEMENTAZIONE AD ALTO LIVELLO
 *   Il kernel scalare di riferimento (sparse_scalar.c) ha, nel ciclo piu'
 *   interno, un ciclo su "col" = 0..3 che aggiorna 4 colonne di output
 *   ADIACENTI con lo stesso valore di input "av":
 *
 *        acc[col] += av * block_values[16p + 4r + col]
 *
 *   Le 4 colonne sono contigue in memoria (row-major nel blocco), quindi
 *   questo ciclo corrisponde esattamente a UNA istruzione SIMD su 4 lane FP32:
 *     - acc          -> un registro float32x4_t (4 accumulatori)
 *     - av           -> broadcast dello scalare su tutte le 4 lane (vdupq_n_f32)
 *     - 4 pesi       -> un caricamento vettoriale da 128 bit (vld1q_f32)
 *     - acc += av*w  -> fused multiply-add (vfmaq_f32)
 *   La struttura dei cicli (i, g, p) e l'ordine di attraversamento dei blocchi
 *   restano identici allo scalare; sparisce il ciclo "col". Si scrive su C una
 *   sola volta per (i, g) con vst1q_f32.
 *
 * LOOP UNROLLING
 *   Il ciclo sulle 4 righe locali del blocco ("r") ha sempre 4 iterazioni: lo
 *   scriviamo per esteso. Si eliminano contatore, confronto e salto del ciclo
 *   e si caricano subito tutte e 4 le righe di pesi del blocco (w0..w3). Il
 *   compilatore a -O0 (default del Makefile) non lo farebbe da solo. L'ordine
 *   delle somme sull'accumulatore resta r = 0,1,2,3, quindi il risultato e'
 *   identico alla versione con il ciclo.
 *
 * NOTA NUMERICA
 *   vfmaq_f32 e' "fused": un solo arrotondamento invece di due (mul + add).
 *   Il risultato puo' differire dallo scalare di pochi ULP; il test usa una
 *   tolleranza relativa/assoluta di 2e-4, quindi e' accettabile.
 * ============================================================================
 */

#include "sparse_gemm.h"   /* tipo sparse_weights, codici di ritorno, prototipi */
#include <arm_neon.h>      /* intrinsics NEON: float32x4_t, vld1q_f32, vfmaq_f32, ... */

int gemm_sparse_neon(const float *a, const sparse_weights *w, float *c, size_t m)
{
    /* Controllo difensivo sui puntatori: se uno e' nullo segnalo errore. */
    if (!a || !w || !c)
        return SPARSE_ERROR;

    /* Numero di gruppi di 4 colonne di output: N / 4 (es. 512/4 = 128). */
    const size_t groups = w->n / 4;

    /* Ciclo sulle righe di A / righe di C (un'uscita di riga alla volta). */
    for (size_t i = 0; i < m; ++i) {

        /* Puntatore all'inizio della riga i di A (row-major, K elementi per riga). */
        const float *a_row = a + i * w->k;

        /* Ciclo sui gruppi di 4 colonne di output (g = 0 .. N/4 - 1). */
        for (size_t g = 0; g < groups; ++g) {

            /* Accumulatore vettoriale: 4 lane FP32 = le 4 colonne 4g..4g+3.
               Parte da zero; vdupq_n_f32(0.0f) replica 0 su tutte le lane. */
            float32x4_t acc = vdupq_n_f32(0.0f);

            /* Ciclo sui blocchi memorizzati del gruppo g: posizioni p
               nell'intervallo [block_col_ptr[g], block_col_ptr[g+1]). I blocchi
               mancanti (tutti zero) non compaiono e vengono quindi saltati. */
            for (size_t p = w->block_col_ptr[g]; p < w->block_col_ptr[g + 1]; ++p) {

                /* Prima riga di W (= primo indice di riduzione k) del blocco p:
                   k0 = 4 * indice di blocco-riga. */
                const size_t k0 = 4 * (size_t)w->block_row_idx[p];

                /* Puntatore ai 16 pesi del blocco p (row-major 4x4). */
                const float *blk = w->block_values + 16 * p;

                /* Puntatore ai 4 valori di input usati dal blocco:
                   x[r] = A[i, k0 + r] per r = 0..3. */
                const float *x = a_row + k0;

                /* Carico le 4 righe di pesi del blocco (4 pesi adiacenti
                   ciascuna): w0 = riga locale 0, w1 = riga 1, w2 = riga 2,
                   w3 = riga 3. Ogni vettore = le 4 colonne locali col=0..3. */
                const float32x4_t w0 = vld1q_f32(blk + 0);
                const float32x4_t w1 = vld1q_f32(blk + 4);
                const float32x4_t w2 = vld1q_f32(blk + 8);
                const float32x4_t w3 = vld1q_f32(blk + 12);

                /* --- riga locale r = 0 ---
                   vdupq_n_f32(x[0]) copia A[i,k0+0] su 4 lane; vfmaq_f32 fa
                   acc[lane] += x0 * w0[lane] per le 4 colonne insieme. */
                acc = vfmaq_f32(acc, vdupq_n_f32(x[0]), w0);

                /* --- riga locale r = 1 --- */
                acc = vfmaq_f32(acc, vdupq_n_f32(x[1]), w1);

                /* --- riga locale r = 2 --- */
                acc = vfmaq_f32(acc, vdupq_n_f32(x[2]), w2);

                /* --- riga locale r = 3 --- */
                acc = vfmaq_f32(acc, vdupq_n_f32(x[3]), w3);
            }

            /* Scrivo i 4 risultati nelle colonne 4g..4g+3 della riga i di C. */
            vst1q_f32(c + i * w->n + 4 * g, acc);
        }
    }

    /* Kernel implementato con successo. */
    return SPARSE_OK;
}
