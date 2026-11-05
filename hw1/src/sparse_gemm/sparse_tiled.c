/*
 * ============================================================================
 *  sparse_tiled.c  --  4x4 block-sparse GEMM con tile di 4 righe (Esercizio 3)
 *                      SENZA NEON (solo C scalare)
 *                      + LOOP UNROLLING di UNA riga di 4 pesi
 * ============================================================================
 *
 * OBIETTIVO
 *   Calcolare C = A * W (W sparsa a blocchi 4x4, formato BCC) elaborando
 *   ESATTAMENTE 4 righe di A alla volta e riusando ogni blocco di pesi, una
 *   volta caricato, per tutte e 4 le righe.
 *
 * IMPLEMENTAZIONE AD ALTO LIVELLO
 *   Nel kernel scalare, per ogni riga i i pesi di ogni blocco vengono riletti
 *   dalla memoria: i pesi (16 float per blocco) sono la parte dominante del
 *   traffico, e per m righe vengono letti m volte.
 *   Qui si introduce un "row tile" di 4 righe (TM = 4):
 *     - per ogni tile di righe e per ogni gruppo g si tengono 4 x 4 = 16
 *       accumulatori acc[ri][col] (ri = riga nel tile, col = colonna);
 *     - per ogni blocco p e per ogni riga locale r si leggono i 4 pesi
 *       w0..w3 UNA volta (restano in variabili locali) e si usano per
 *       aggiornare tutte e 4 le righe del tile.
 *   Risultato: ogni peso caricato serve 4 MAC invece di 1, quindi il traffico
 *   di memoria sui pesi cala di circa 4x (maggiore intensita' operazionale).
 *
 * LOOP UNROLLING (solo una riga di 4 pesi)
 *   Il ciclo sulle 4 colonne locali ("col") ha sempre 4 iterazioni: e' scritto
 *   per esteso. Cioe' i 4 pesi di UNA riga del blocco sono 4 variabili con nome
 *   (w0..w3) e le 4 MAC per riga del tile sono scritte una per una. Restano
 *   normali cicli: il ciclo sulle righe locali del blocco ("r"), il ciclo sulle
 *   righe del tile ("ri") e i cicli esterni (tile, gruppo, blocco).
 *   L'ordine delle somme su ogni accumulatore resta r = 0,1,2,3: il risultato e'
 *   identico a gemm_sparse_scalar.
 *
 *   Se m non e' multiplo di 4, le righe residue sono gestite a parte con il
 *   percorso a una riga (con M=128 non succede, ma rende il kernel generale).
 * ============================================================================
 */

#include "sparse_gemm.h"   /* tipo sparse_weights, codici di ritorno, prototipi */

/* Righe elaborate insieme nel tile (fissato a 4 dalla consegna). */
#define TM 4

int gemm_sparse_tiled(const float *a, const sparse_weights *w, float *c,
                      size_t m)
{
    /* Controllo difensivo sui puntatori. */
    if (!a || !w || !c)
        return SPARSE_ERROR;

    /* Numero di gruppi di 4 colonne di output: N / 4. */
    const size_t groups = w->n / 4;

    /* Numero di righe coperte da tile completi: m arrotondato per difetto a
       multiplo di 4. Le restanti (m - m_full) sono gestite dopo. */
    const size_t m_full = m - (m % TM);

    /* ---------- Parte principale: tile completi di 4 righe ---------- */
    for (size_t i0 = 0; i0 < m_full; i0 += TM) {

        /* Ciclo sui gruppi di 4 colonne di output. */
        for (size_t g = 0; g < groups; ++g) {

            /* 16 accumulatori: acc[ri][col], ri = riga del tile, col = colonna
               locale nel gruppo. Tutti inizializzati a zero. */
            float acc[TM][4] = {{0.0f}};

            /* Blocchi memorizzati del gruppo g: p in [col_ptr[g], col_ptr[g+1]). */
            for (size_t p = w->block_col_ptr[g]; p < w->block_col_ptr[g + 1]; ++p) {

                /* Primo indice di riduzione del blocco: k0 = 4 * blocco-riga. */
                const size_t k0 = 4 * (size_t)w->block_row_idx[p];

                /* Puntatore ai 16 pesi del blocco p (row-major 4x4). */
                const float *blk = w->block_values + 16 * p;

                /* Righe locali del blocco r = 0..3 (indice di riduzione k0+r). */
                for (size_t r = 0; r < 4; ++r) {

                    /* Carico UNA volta i 4 pesi della riga locale r (srotolamento
                       del ciclo col): restano in variabili e verranno riusati per
                       tutte e 4 le righe del tile. */
                    const float w0 = blk[4 * r + 0];
                    const float w1 = blk[4 * r + 1];
                    const float w2 = blk[4 * r + 2];
                    const float w3 = blk[4 * r + 3];

                    /* Riuso dei pesi sulle 4 righe del tile. */
                    for (size_t ri = 0; ri < TM; ++ri) {

                        /* Valore di input A[i0+ri, k0+r]. */
                        const float av = a[(i0 + ri) * w->k + k0 + r];

                        /* 4 MAC (una per colonna locale), scritte per esteso. */
                        acc[ri][0] += av * w0;
                        acc[ri][1] += av * w1;
                        acc[ri][2] += av * w2;
                        acc[ri][3] += av * w3;
                    }
                }
            }

            /* Scrittura dei 4x4 risultati del tile in C (una volta sola). */
            for (size_t ri = 0; ri < TM; ++ri)
                for (size_t col = 0; col < 4; ++col)
                    c[(i0 + ri) * w->n + 4 * g + col] = acc[ri][col];
        }
    }

    /* ---------- Residuo: righe rimanenti se m non e' multiplo di 4 ---------- */
    for (size_t i = m_full; i < m; ++i) {
        for (size_t g = 0; g < groups; ++g) {
            /* Percorso scalare a una riga, identico a gemm_sparse_scalar. */
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (size_t p = w->block_col_ptr[g]; p < w->block_col_ptr[g + 1]; ++p) {
                const size_t k0 = 4 * (size_t)w->block_row_idx[p];
                for (size_t r = 0; r < 4; ++r) {
                    const float av = a[i * w->k + k0 + r];
                    for (size_t col = 0; col < 4; ++col)
                        acc[col] += av * w->block_values[16 * p + 4 * r + col];
                }
            }
            for (size_t col = 0; col < 4; ++col)
                c[i * w->n + 4 * g + col] = acc[col];
        }
    }

    /* Kernel completato con successo. */
    return SPARSE_OK;
}
