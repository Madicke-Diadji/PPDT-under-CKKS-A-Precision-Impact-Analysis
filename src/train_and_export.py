#!/usr/bin/env python3
"""
train_and_export.py - Entraine un arbre de decision "hard" (sklearn)
et exporte sa structure au format CSV pour import dans le POC C++.

Usage :
    python3 train_and_export.py --dataset iris --depth 4 --output ../data/tree.csv

Formats d'export :
    CSV : node_id, is_leaf, feature, threshold, left_id, right_id, class_label, min_gap
    JSON: arbre recursif (compatible TreeExporter::loadFromJSON)
"""

import argparse
import csv
import json
import os
import urllib.request

import numpy as np
from sklearn.datasets import load_breast_cancer, make_classification
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import MinMaxScaler
from sklearn.tree import DecisionTreeClassifier

UCI_IRIS_URL = "https://archive.ics.uci.edu/ml/machine-learning-databases/iris/iris.data"


def load_uci_iris():
    feature_names = [
        "sepal length",
        "sepal width",
        "petal length",
        "petal width",
    ]
    label_to_id = {
        "Iris-setosa": 0,
        "Iris-versicolor": 1,
        "Iris-virginica": 2,
    }

    print(f"Chargement Iris depuis UCI: {UCI_IRIS_URL}")
    with urllib.request.urlopen(UCI_IRIS_URL, timeout=30) as response:
        lines = response.read().decode("utf-8").splitlines()

    X, y = [], []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split(",")
        if len(parts) != 5:
            raise ValueError(f"Ligne Iris invalide: {line}")
        X.append([float(v) for v in parts[:4]])
        y.append(label_to_id[parts[4]])

    return np.asarray(X, dtype=np.float64), np.asarray(y, dtype=np.int64), feature_names


def load_dataset(name):
    if name == "iris":
        return load_uci_iris()
    if name == "breast_cancer":
        data = load_breast_cancer()
        return data.data, data.target, data.feature_names
    if name == "synthetic":
        X, y = make_classification(
            n_samples=1000,
            n_features=4,
            n_classes=2,
            random_state=42,
        )
        return X, y, [f"X{i}" for i in range(4)]
    raise ValueError(f"Dataset inconnu: {name}")


def load_local_csv_dataset(prefix):
    train_path = f"{prefix}_train.csv"
    test_path = f"{prefix}_test.csv"

    if not os.path.exists(train_path):
        raise FileNotFoundError(f"Fichier train introuvable: {train_path}")
    if not os.path.exists(test_path):
        raise FileNotFoundError(f"Fichier test introuvable: {test_path}")

    def read_one(path):
        with open(path, newline="", encoding="utf-8") as f:
            reader = csv.reader(f)
            header = next(reader)
            if len(header) < 2:
                raise ValueError(f"CSV invalide: {path}")
            feature_names = header[1:]
            X = []
            y = []
            for row in reader:
                if not row:
                    continue
                y.append(int(row[0]))
                X.append([float(v) for v in row[1:]])
        return np.asarray(X, dtype=np.float64), np.asarray(y, dtype=np.int64), feature_names

    X_train, y_train, feature_names = read_one(train_path)
    X_test, y_test, test_feature_names = read_one(test_path)

    if feature_names != test_feature_names:
        raise ValueError("Les headers train/test ne correspondent pas.")

    print(f"Chargement dataset local depuis {train_path} et {test_path}")
    return X_train, X_test, y_train, y_test, feature_names


def train_hard_tree(X_train, y_train, max_depth=4):
    tree = DecisionTreeClassifier(max_depth=max_depth, random_state=42)
    tree.fit(X_train, y_train)
    return tree


def collect_min_gaps(tree_clf, X_train):
    sk_tree = tree_clf.tree_
    n_nodes = sk_tree.node_count
    min_gaps = [float("inf")] * n_nodes

    node_indicator = tree_clf.decision_path(X_train)

    for sample_idx in range(X_train.shape[0]):
        node_ids = node_indicator[sample_idx].indices
        for node_id in node_ids:
            if sk_tree.children_left[node_id] == -1:
                continue
            feat = sk_tree.feature[node_id]
            thresh = sk_tree.threshold[node_id]
            gap = abs(X_train[sample_idx, feat] - thresh)
            if gap < min_gaps[node_id]:
                min_gaps[node_id] = gap

    return min_gaps


def export_csv(tree_clf, min_gaps, output_path, nb_classes):
    del nb_classes
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    sk_tree = tree_clf.tree_
    n_nodes = sk_tree.node_count

    rows = []
    for node_id in range(n_nodes):
        is_leaf = sk_tree.children_left[node_id] == -1
        feature = int(sk_tree.feature[node_id]) if not is_leaf else -1
        threshold = float(sk_tree.threshold[node_id]) if not is_leaf else 0.0
        left_id = int(sk_tree.children_left[node_id])
        right_id = int(sk_tree.children_right[node_id])
        class_label = int(np.argmax(sk_tree.value[node_id][0]))
        gap = float(min_gaps[node_id]) if min_gaps[node_id] != float("inf") else 0.5
        rows.append([
            node_id,
            int(is_leaf),
            feature,
            threshold,
            left_id,
            right_id,
            class_label,
            gap,
        ])

    with open(output_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "node_id",
            "is_leaf",
            "feature",
            "threshold",
            "left_id",
            "right_id",
            "class_label",
            "min_gap",
        ])
        writer.writerows(rows)

    print(f"Arbre exporte -> {output_path} ({n_nodes} noeuds)")


def export_json(tree_clf, min_gaps, output_path, nb_classes):
    del nb_classes
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    sk_tree = tree_clf.tree_

    def build_node(node_id):
        node_id = int(node_id)
        is_leaf = sk_tree.children_left[node_id] == -1
        label = int(np.argmax(sk_tree.value[node_id][0]))
        total = float(sk_tree.value[node_id][0].sum())
        leaf_val = [float(v / total) for v in sk_tree.value[node_id][0]]

        if is_leaf:
            return {
                "node_id": node_id,
                "is_leaf": True,
                "class_label": label,
                "leaf_value": leaf_val,
            }

        return {
            "node_id": node_id,
            "is_leaf": False,
            "feature": int(sk_tree.feature[node_id]),
            "threshold": float(sk_tree.threshold[node_id]),
            "min_gap": float(min_gaps[node_id]) if min_gaps[node_id] != float("inf") else 0.5,
            "left": build_node(sk_tree.children_left[node_id]),
            "right": build_node(sk_tree.children_right[node_id]),
        }

    tree_dict = build_node(0)
    with open(output_path, "w") as f:
        json.dump(tree_dict, f, indent=2)
    print(f"Arbre exporte -> {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Entraine + exporte un arbre de decision")
    parser.add_argument("--dataset", default="iris", choices=["iris", "breast_cancer", "synthetic"])
    parser.add_argument(
        "--data-prefix",
        default="",
        help="Prefixe local pour charger data/<nom>_train.csv et data/<nom>_test.csv",
    )
    parser.add_argument("--depth", type=int, default=4, help="Profondeur max de l'arbre")
    parser.add_argument("--output", default="../data/tree.csv", help="Fichier de sortie CSV")
    parser.add_argument("--json", default="../data/tree.json", help="Fichier de sortie JSON")
    args = parser.parse_args()

    if args.data_prefix:
        X_train, X_test, y_train, y_test, feat_names = load_local_csv_dataset(args.data_prefix)
    else:
        X, y, feat_names = load_dataset(args.dataset)
        scaler = MinMaxScaler()
        X = scaler.fit_transform(X).astype(np.float64)
        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=0.2, random_state=42
        )

    clf = train_hard_tree(X_train, y_train, max_depth=args.depth)
    acc = clf.score(X_test, y_test)
    nb_classes = len(np.unique(np.concatenate((y_train, y_test))))

    print(f"\nArbre entraîne : depth={args.depth}, n_nodes={clf.tree_.node_count}")
    print(f"Précision test  : {acc * 100:.2f}%  ({len(y_test)} samples)")
    print(f"Features        : {list(feat_names)}")
    print(f"Classes         : {nb_classes}")

    min_gaps = collect_min_gaps(clf, X_train)
    non_inf_gaps = [g for g in min_gaps if g != float("inf")]
    print(f"Min gap global  : {min(non_inf_gaps):.4f}")
    print(f"Median gap      : {np.median(non_inf_gaps):.4f}")

    export_csv(clf, min_gaps, args.output, nb_classes)
    export_json(clf, min_gaps, args.json, nb_classes)

    test_path = args.output.replace("tree.csv", "test_data.csv")
    os.makedirs(os.path.dirname(test_path) or ".", exist_ok=True)
    with open(test_path, "w", newline="") as f:
        writer = csv.writer(f)
        header = [f"X{i}" for i in range(X_test.shape[1])] + ["label"]
        writer.writerow(header)
        for xi, yi in zip(X_test, y_test):
            writer.writerow(list(xi) + [int(yi)])
    print(f"Dataset test exporte -> {test_path}")


if __name__ == "__main__":
    main()
