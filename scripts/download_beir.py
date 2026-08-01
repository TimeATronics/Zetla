from datasets import load_dataset
import os

dsets = [
    ('BeIR/nfcorpus', 'nfcorpus'),
    ('BeIR/arguana', 'arguana'),
    ('BeIR/fiqa', 'fiqa'),
    ('BeIR/scidocs', 'scidocs'),
    # cqadupstack is large, skip for now
]

for ds, name in dsets:
    dir = f'beir_bench/{name}'
    os.makedirs(dir, exist_ok=True)
    try:
        # Corpus
        d = load_dataset(ds, 'corpus', split='corpus', trust_remote_code=True)
        with open(f'{dir}/corpus.tsv','w',encoding='utf-8') as f:
            for r in d:
                tid = str(r.get('_id', r.get('id', '')))
                title = str(r.get('title',''))
                txt = str(r.get('text','')).replace('\t',' ').replace('\n',' ')
                f.write(f"{tid}\t{title}\t{txt}\n")
        # Queries
        dq = load_dataset(ds, 'queries', split='queries', trust_remote_code=True)
        with open(f'{dir}/queries.tsv','w',encoding='utf-8') as f:
            for r in dq:
                qid = str(r.get('_id', r.get('id', '')))
                qtxt = str(r.get('text','')).replace('\t',' ').replace('\n',' ')
                f.write(f"{qid}\t{qtxt}\n")
        print(f'{name}: {len(d)} docs, {len(dq)} queries')
    except Exception as e:
        print(f'{name}: ERROR {e}')
