#!/usr/bin/env python3
import os, math, json, random, argparse
from pathlib import Path
import numpy as np

# ---------- utils ----------
def set_seed(seed: int):
    random.seed(seed)
    np.random.seed(seed)

def ensure_dir(p): os.makedirs(p, exist_ok=True)

def relu(x): return np.maximum(x, 0.0)
def gelu(x):
    # tanh-based approximation
    return 0.5 * x * (1.0 + np.tanh(np.sqrt(2.0/np.pi) * (x + 0.044715 * np.power(x,3))))

def save_arr(path, arr: np.ndarray):
    np.save(path, arr.astype(np.float32), allow_pickle=False)

# Keep-top-|.| entries to hit target density
def keep_topk_density(arr: np.ndarray, target_density: float):
    m, n = arr.shape
    total = m * n
    k = max(1, int(total * target_density))
    if k >= total: 
        return arr
    flat = np.abs(arr).ravel()
    # threshold is the k-th largest absolute value
    thresh = np.partition(flat, -k)[-k]
    mask = np.abs(arr) >= thresh
    out = np.zeros_like(arr, dtype=np.float32)
    out[mask] = arr[mask]
    return out

# ---------- synthetic patterns (dense arrays with zeros) ----------
def synth_uniform(m, n, density, seed=0, dist="normal"):
    set_seed(seed)
    nnz = int(m * n * density)
    arr = np.zeros((m, n), dtype=np.float32)
    # choose unique coordinates
    idx = np.random.choice(m*n, size=nnz, replace=False)
    rows, cols = np.divmod(idx, n)
    if dist == "normal":
        vals = np.random.randn(nnz).astype(np.float32)
    else:
        vals = (np.random.rand(nnz).astype(np.float32) * 2 - 1)
    arr[rows, cols] = vals
    return arr

def synth_block(m, n, density, block=16, seed=0, dist="normal"):
    set_seed(seed)
    bm, bn = math.ceil(m/block), math.ceil(n/block)
    total_blocks = bm * bn
    # approximate density by turning on a subset of blocks
    active_blocks = max(1, int(total_blocks * density))
    chosen = set(np.random.choice(total_blocks, size=active_blocks, replace=False))
    arr = np.zeros((m, n), dtype=np.float32)
    for bi in range(bm):
        for bj in range(bn):
            if bi*bn + bj in chosen:
                r0, r1 = bi*block, min((bi+1)*block, m)
                c0, c1 = bj*block, min((bj+1)*block, n)
                if dist == "normal":
                    arr[r0:r1, c0:c1] = np.random.randn(r1-r0, c1-c0).astype(np.float32)
                else:
                    arr[r0:r1, c0:c1] = (np.random.rand(r1-r0, c1-c0).astype(np.float32)*2 - 1)
    # may overshoot density slightly; trim to target density strictly:
    return keep_topk_density(arr, density)

def synth_powerlaw(m, n, density, alpha=1.5, seed=0, dist="normal"):
    set_seed(seed)
    nnz = int(m * n * density)
    arr = np.zeros((m, n), dtype=np.float32)
    ranks = np.arange(1, n+1)
    probs = 1.0 / (ranks ** alpha)
    probs = probs / probs.sum()
    cols = np.random.choice(n, size=nnz, p=probs)
    rows = np.random.randint(0, m, size=nnz)
    if dist == "normal":
        vals = np.random.randn(nnz).astype(np.float32)
    else:
        vals = (np.random.rand(nnz).astype(np.float32) * 2 - 1)
    arr[rows, cols] = vals
    return arr

# ---------- pruning / activations without PyTorch ----------
def magnitude_prune(W: np.ndarray, target_density: float):
    return keep_topk_density(W, target_density)

def relu_topk_activations(X: np.ndarray, target_density: float):
    return keep_topk_density(relu(X), target_density)

def gelu_topk_activations(X: np.ndarray, target_density: float):
    return keep_topk_density(gelu(X), target_density)

# ---------- curated NN-ish makers (square downscaled) ----------
def cnn_fc_dualsparse(shape=1024, densA=0.10, densB=0.10, seed=0):
    set_seed(seed)
    X = np.random.randn(shape, shape).astype(np.float32)
    A = relu_topk_activations(X, densA)               # activations after ReLU → sparse
    W = np.random.randn(shape, shape).astype(np.float32)
    B = magnitude_prune(W, densB)                     # pruned weights → sparse
    return A, B

def cnn_1x1_dualsparse(shape=1024, densA=0.10, densB=0.10, seed=0):
    # modeled as GEMM; same generator as FC
    return cnn_fc_dualsparse(shape, densA, densB, seed)

def llm_ffn_dualsparse(shape=1024, densA=0.10, densB=0.10, seed=0):
    set_seed(seed)
    X = np.random.randn(shape, shape).astype(np.float32)
    A = gelu_topk_activations(X, densA)               # GELU sparsified
    W = np.random.randn(shape, shape).astype(np.float32)
    B = magnitude_prune(W, densB)
    return A, B

def llm_attn_qkt_dualsparse(shape=1024, densQ=0.10, densK=0.10, block=32, seed=0):
    # Build block-sparse Q and K, then form A = Q, B = K^T to represent Q * K^T as A×B
    Q = synth_block(shape, shape, densQ, block=block, seed=seed)
    K = synth_block(shape, shape, densK, block=block, seed=seed+1)
    return Q, K.T

# ---------- workload builders ----------
def build_synth_items(sizes, dens_list, patterns):
    items = []
    for s in sizes:
        for da in dens_list:
            for db in dens_list:
                for p in patterns:
                    items.append(dict(
                        wid=f"synth-{p}-s{s}-da{da}-db{db}",
                        kind="synth", m=s, k=s, n=s,
                        densA=da, densB=db, pattern=p,
                        block=(16 if s<=256 else 32), alpha=1.5
                    ))
    return items

def realize_item(it, out_root, seed):
    out_dir = os.path.join(out_root, it["wid"])
    ensure_dir(out_dir)

    # A
    if it["kind"] == "synth":
        p = it["pattern"]
        if p == "uniform":
            A = synth_uniform(it["m"], it["k"], it["densA"], seed=seed)
            B = synth_uniform(it["k"], it["n"], it["densB"], seed=seed+1)
        elif p == "block":
            A = synth_block(it["m"], it["k"], it["densA"], block=it["block"], seed=seed)
            B = synth_block(it["k"], it["n"], it["densB"], block=it["block"], seed=seed+1)
        elif p == "powerlaw":
            A = synth_powerlaw(it["m"], it["k"], it["densA"], alpha=it["alpha"], seed=seed)
            B = synth_powerlaw(it["k"], it["n"], it["densB"], alpha=it["alpha"], seed=seed+1)
        else:
            raise ValueError("unknown pattern")
    elif it["kind"] == "cnn-fc":
        A, B = cnn_fc_dualsparse(it["m"], it["densA"], it["densB"], seed)
    elif it["kind"] == "cnn-1x1":
        A, B = cnn_1x1_dualsparse(it["m"], it["densA"], it["densB"], seed)
    elif it["kind"] == "llm-ffn":
        A, B = llm_ffn_dualsparse(it["m"], it["densA"], it["densB"], seed)
    elif it["kind"] == "llm-attn-qkt":
        A, B = llm_attn_qkt_dualsparse(it["m"], it["densA"], it["densB"], block=it["block"], seed=seed)
    else:
        raise ValueError("unknown kind")

    save_arr(os.path.join(out_dir, "A.npy"), A)
    save_arr(os.path.join(out_dir, "B.npy"), B)

    # tiny companion for sanity (optional; delete if you truly want only npy files)
    meta = dict(id=it["wid"], kind=it["kind"], m=int(A.shape[0]), k=int(A.shape[1]),
                n=int(B.shape[1]), densityA=float((A!=0).mean()), densityB=float((B!=0).mean()))
    with open(os.path.join(out_dir, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)

    return meta

def build_curated_items():
    items = []
    for da, db in [(0.10,0.10),(0.20,0.20)]:
        items.append(dict(wid=f"cnn-fc-s1024-da{da}-db{db}", kind="cnn-fc", m=1024,k=1024,n=1024,
                          densA=da, densB=db, pattern="relu-topk", block=32, alpha=1.5))
        items.append(dict(wid=f"cnn-1x1-s1024-da{da}-db{db}", kind="cnn-1x1", m=1024,k=1024,n=1024,
                          densA=da, densB=db, pattern="relu-topk", block=32, alpha=1.5))
        items.append(dict(wid=f"llm-ffn-s1024-da{da}-db{db}", kind="llm-ffn", m=1024,k=1024,n=1024,
                          densA=da, densB=db, pattern="gelu-topk", block=32, alpha=1.5))
    for da, db, b in [(0.05,0.05,32),(0.10,0.10,32)]:
        items.append(dict(wid=f"llm-attn-qkt-s1024-da{da}-db{db}-b{b}", kind="llm-attn-qkt", m=1024,k=1024,n=1024,
                          densA=da, densB=db, pattern=f"block({b})", block=b, alpha=1.5))
    return items

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="out_ndarray")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--only", choices=["synth","curated","all"], default="all")
    ap.add_argument("--sizes", default="256,512,1024")
    ap.add_argument("--dens",  default="0.05,0.10,0.20")
    ap.add_argument("--patterns", default="uniform,block,powerlaw")
    args = ap.parse_args()

    # Make output directory relative to benchmark folder
    script_dir = Path(__file__).parent
    if not os.path.isabs(args.out):
        output_dir = script_dir / args.out
    else:
        output_dir = Path(args.out)

    set_seed(args.seed)
    ensure_dir(output_dir)

    items = []
    if args.only in ("all","synth"):
        sizes = tuple(int(s) for s in args.sizes.split(","))
        dens = tuple(float(x) for x in args.dens.split(","))
        patterns = tuple(p.strip() for p in args.patterns.split(","))
        items += build_synth_items(sizes, dens, patterns)
    if args.only in ("all","curated"):
        items += build_curated_items()

    metas = []
    for i, it in enumerate(items):
        metas.append(realize_item(it, str(output_dir), seed=args.seed + i))

    # global manifest (optional)
    with open(os.path.join(str(output_dir), "manifest.json"), "w") as f:
        json.dump({"benchmarks": metas}, f, indent=2)

    print(f"Generated {len(items)} workloads → {output_dir}")

if __name__ == "__main__":
    main()
