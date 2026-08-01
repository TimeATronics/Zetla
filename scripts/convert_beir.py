import json, os, glob, shutil

for ds in ['nfcorpus','arguana','scidocs','fiqa']:
    dir = f'beir_bench/{ds}'
    
    # Qrels: check nested dir structure from beir zip
    qsrc = None
    for pattern in [f'{dir}/{ds}/qrels/test.tsv', f'{dir}/{ds}/qrels/dev.tsv',
                    f'{dir}/qrels/test.tsv', f'{dir}/qrels.tsv']:
        if os.path.exists(pattern):
            qsrc = pattern
            break
    
    if qsrc and qsrc != f'{dir}/qrels.tsv':
        shutil.copy(qsrc, f'{dir}/qrels.tsv')
        print(f'{ds}: qrels copied from {qsrc}')
        
    nq = 0
    if os.path.exists(f'{dir}/qrels.tsv'):
        with open(f'{dir}/qrels.tsv', encoding='utf-8') as f:
            nq = sum(1 for _ in f)
    nc = 0
    if os.path.exists(f'{dir}/corpus.tsv'):
        with open(f'{dir}/corpus.tsv', encoding='utf-8') as f:
            nc = sum(1 for _ in f)
    print(f'  {ds}: {nc} docs, {nq} qrels')
