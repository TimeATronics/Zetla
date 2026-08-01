from datasets import load_dataset, get_dataset_config_names

dsets = [
    ('BeIR/nfcorpus', 'nfcorpus'),
    ('BeIR/arguana', 'arguana'),
    ('BeIR/scidocs', 'scidocs'),
    ('BeIR/fiqa', 'fiqa'),
]

for ds, name in dsets:
    try:
        # The qrels are usually the 'test' split
        d = load_dataset(ds, split='test')
        # Print first row to see the structure
        print(f'{name}: columns={list(d.features.keys())}, {len(d)} rows')
        path = f'beir_bench/{name}/qrels.tsv'
        with open(path,'w',encoding='utf-8') as f:
            for r in d:
                # Try different possible field names
                qid = str(r.get('query-id', r.get('qid', r.get('query_id', ''))))
                did = str(r.get('corpus-id', r.get('docid', r.get('doc_id', r.get('_id', '')))))
                score = str(r.get('score', 1))
                if qid and did:
                    f.write(f"{qid}\t{did}\t{score}\n")
        print(f'  -> wrote qrels')
    except Exception as e:
        print(f'{name}: ERROR {e}')
