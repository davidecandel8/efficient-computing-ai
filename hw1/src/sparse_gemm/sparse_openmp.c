/*
 * ============================================================================
 *  sparse_openmp.c  --  4x4 block-sparse GEMM parallelizzato con OpenMP
 *                       (Esercizio 4 della lista; nessun tiling, nessun NEON)
 * ============================================================================
 *
 * OBIETTIVO
 *   Calcolare C = A * W (W sparsa a blocchi 4x4, formato BCC) sfruttando i 4
 *   core del Cortex-A72 del Raspberry Pi 4 tramite OpenMP.
 *
 * IMPLEMENTAZIONE AD ALTO LIVELLO
 *   Il kernel e' quello scalare di riferimento. Le righe di output sono
 *   completamente indipendenti: la riga i di C dipende solo dalla riga i di A
 *   e da W (letta e mai scritta). Quindi basta distribuire le iterazioni del
 *   ciclo esterno su i tra i thread con un semplice
 *       #pragma omp parallel for
 *   senza clausole (niente collapse / schedule / reduction, come richiesto).
 *   - Nessuna race condition: ogni thread scrive solo righe di C distinte.
 *   - Tutte le variabili di lavoro (g, p, k0, r, col, acc, av) sono dichiarate
 *     DENTRO il ciclo, quindi sono private a ciascun thread.
 *   - W (block_col_ptr, block_row_idx, block_values) e A sono solo letti.
 *   Il numero di thread e' deciso dall'esterno (omp_set_num_threads nei
 *   test/benchmark, oppure OMP_NUM_THREADS).
 * ============================================================================
 */

#include "sparse_gemm.h"   /* tipo sparse_weights, codici di ritorno, prototipi */

int gemm_sparse_openmp(const float *a, const sparse_weights *w, float *c, size_t m)
{
    /* Controllo difensivo sui puntatori. */
    if (!a || !w || !c)
        return SPARSE_ERROR;

    /* Numero di gruppi di 4 colonne di output: N / 4. Calcolato fuori dalla
       regione parallela; e' di sola lettura per i thread (shared). */
    const size_t groups = w->n / 4;

    /* Parallelizzo SOLO il ciclo sulle righe di output. Senza clausole: il
       default schedule distribuisce le iterazioni a blocchi contigui. La guardia
       #ifdef evita errori/warning se compilato senza -fopenmp (pragma ignorato). */
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (size_t i = 0; i < m; ++i) {

        /* Ciclo sui gruppi di 4 colonne di output per la riga i. */
        for (size_t g = 0; g < groups; ++g) {

            /* 4 accumulatori locali (privati al thread) per le colonne 4g..4g+3. */
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};

            /* Blocchi memorizzati del gruppo g: p in [col_ptr[g], col_ptr[g+1]). */
            for (size_t p = w->block_col_ptr[g]; p < w->block_col_ptr[g + 1]; ++p) {

                /* Primo indice di riduzione del blocco: k0 = 4 * blocco-riga. */
                size_t k0 = 4 * (size_t)w->block_row_idx[p];

                /* Righe locali del blocco r = 0..3. */
                for (size_t r = 0; r < 4; ++r) {

                    /* Elemento di input A[i, k0+r]. */
                    float av = a[i * w->k + k0 + r];

                    /* Aggiorno le 4 colonne locali con i pesi del blocco p. */
                    for (size_t col = 0; col < 4; ++col)
                        acc[col] += av * w->block_values[16 * p + 4 * r + col];
                }
            }

            /* Scrivo i 4 risultati: riga i e' esclusiva di questo thread. */
            for (size_t col = 0; col < 4; ++col)
                c[i * w->n + 4 * g + col] = acc[col];
        }
    }

    /* Kernel completato con successo. */
    return SPARSE_OK;
}
