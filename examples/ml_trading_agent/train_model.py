#!/usr/bin/env python3
"""
Train ML Model for Trading Agent

This script trains a decision tree classifier for trading signal generation
and exports it in the format expected by the slonana ML inference syscalls.

Usage:
    python train_model.py --input data.csv --output model.bin
    python train_model.py --generate-sample --output model.bin
"""

import argparse
import struct
import numpy as np
from sklearn.tree import DecisionTreeClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report

# Fixed-point scale (must match slonana SCALE constant)
SCALE = 10000


def generate_sample_data(n_samples=1000):
    """Generate synthetic market data for training."""
    np.random.seed(42)
    
    # Features:
    # 0: price_ratio (current / average)
    # 1: volume_ratio (current / average)
    # 2: momentum (1h price change in bp)
    # 3: spread (bid-ask spread in bp)
    # 4: volatility (24h range / average)
    
    X = np.zeros((n_samples, 5))
    y = np.zeros(n_samples, dtype=int)
    
    for i in range(n_samples):
        # Random market conditions
        trend = np.random.choice([-1, 0, 1], p=[0.3, 0.4, 0.3])
        
        # Generate features based on trend
        if trend == 1:  # Bullish
            X[i, 0] = np.random.normal(1.02, 0.02)  # Price above average
            X[i, 1] = np.random.normal(1.2, 0.3)    # Higher volume
            X[i, 2] = np.random.normal(200, 100)    # Positive momentum
            X[i, 3] = np.random.normal(30, 15)      # Tighter spread
            X[i, 4] = np.random.normal(0.03, 0.01)  # Normal volatility
        elif trend == -1:  # Bearish
            X[i, 0] = np.random.normal(0.97, 0.02)  # Price below average
            X[i, 1] = np.random.normal(0.9, 0.2)    # Lower volume
            X[i, 2] = np.random.normal(-200, 100)   # Negative momentum
            X[i, 3] = np.random.normal(80, 20)      # Wider spread
            X[i, 4] = np.random.normal(0.05, 0.02)  # Higher volatility
        else:  # Neutral
            X[i, 0] = np.random.normal(1.0, 0.01)   # Price at average
            X[i, 1] = np.random.normal(1.0, 0.1)    # Normal volume
            X[i, 2] = np.random.normal(0, 50)       # No momentum
            X[i, 3] = np.random.normal(50, 20)      # Normal spread
            X[i, 4] = np.random.normal(0.04, 0.01)  # Normal volatility
        
        # Label: 0=SELL(-1 in fixed), 1=HOLD, 2=BUY
        # Add some noise to labels
        if np.random.random() < 0.9:
            y[i] = trend + 1  # Map -1,0,1 to 0,1,2
        else:
            y[i] = np.random.randint(0, 3)  # Random label (noise)
    
    return X, y


def train_decision_tree(X, y, max_depth=5):
    """Train a decision tree classifier."""
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42
    )
    
    clf = DecisionTreeClassifier(
        max_depth=max_depth,
        min_samples_split=5,
        min_samples_leaf=3,
        random_state=42
    )
    
    clf.fit(X_train, y_train)
    
    # Evaluate
    y_pred = clf.predict(X_test)
    print("\n=== Model Evaluation ===")
    print(classification_report(y_test, y_pred, 
                                target_names=['SELL', 'HOLD', 'BUY']))
    
    return clf


def export_tree_to_slonana(clf, filename):
    """Export decision tree to slonana binary format."""
    tree = clf.tree_
    
    # Count non-leaf and leaf nodes
    n_nodes = tree.node_count
    
    print(f"\n=== Exporting Model ===")
    print(f"Total nodes: {n_nodes}")
    print(f"Max depth: {tree.max_depth}")
    print(f"Features used: {set(tree.feature[tree.feature >= 0])}")
    
    with open(filename, 'wb') as f:
        # Header
        f.write(struct.pack('<I', 1))  # version
        f.write(struct.pack('<H', n_nodes))  # num_nodes
        f.write(struct.pack('<H', tree.max_depth))  # max_depth
        f.write(struct.pack('<I', clf.n_features_in_))  # num_features
        f.write(struct.pack('<I', clf.n_classes_))  # num_classes
        f.write(struct.pack('<Q', 0))  # timestamp (placeholder)
        
        # Nodes (DecisionTreeNode structure: 16 bytes each)
        for i in range(n_nodes):
            if tree.children_left[i] == -1:  # Leaf node
                # feature_index = -1 for leaf
                f.write(struct.pack('<h', -1))  # feature_index
                f.write(struct.pack('<h', -1))  # left_child
                f.write(struct.pack('<h', -1))  # right_child
                f.write(struct.pack('<h', 0))   # padding
                f.write(struct.pack('<i', 0))   # threshold
                
                # Get class prediction and convert to signal
                # Classes are [0,1,2] → signals are [-1,0,1]
                class_idx = int(np.argmax(tree.value[i]))
                signal = class_idx - 1  # Map 0,1,2 to -1,0,1
                f.write(struct.pack('<i', signal))  # leaf_value
            else:  # Internal node
                f.write(struct.pack('<h', tree.feature[i]))  # feature_index
                f.write(struct.pack('<h', tree.children_left[i]))  # left_child
                f.write(struct.pack('<h', tree.children_right[i]))  # right_child
                f.write(struct.pack('<h', 0))  # padding
                
                # Convert threshold to fixed-point
                # Features are already scaled, but threshold needs conversion
                threshold = int(tree.threshold[i] * SCALE)
                f.write(struct.pack('<i', threshold))  # threshold
                f.write(struct.pack('<i', 0))  # leaf_value (unused)
    
    print(f"Model exported to: {filename}")
    print(f"File size: {n_nodes * 16 + 24} bytes")


def export_tree_to_cpp(clf, filename):
    """Export decision tree as C++ code for embedding."""
    tree = clf.tree_
    n_nodes = tree.node_count
    
    cpp_code = f"""// Auto-generated decision tree model
// Generated by train_model.py

#include <array>

// Model metadata
constexpr uint32_t MODEL_VERSION = 1;
constexpr uint32_t MODEL_NODE_COUNT = {n_nodes};
constexpr uint32_t MODEL_MAX_DEPTH = {tree.max_depth};
constexpr uint32_t MODEL_NUM_FEATURES = {clf.n_features_in_};
constexpr uint32_t MODEL_NUM_CLASSES = {clf.n_classes_};

// Decision tree nodes
constexpr DecisionTreeNode TRADING_MODEL[MODEL_NODE_COUNT] = {{
"""
    
    for i in range(n_nodes):
        if tree.children_left[i] == -1:  # Leaf
            class_idx = int(np.argmax(tree.value[i]))
            signal = class_idx - 1
            cpp_code += f"    {{-1, -1, -1, 0, 0, {signal}}},  // Node {i}: leaf = {['SELL','HOLD','BUY'][class_idx]}\n"
        else:  # Internal
            threshold = int(tree.threshold[i] * SCALE)
            cpp_code += f"    {{{tree.feature[i]}, {tree.children_left[i]}, {tree.children_right[i]}, 0, {threshold}, 0}},  // Node {i}: feature {tree.feature[i]} < {tree.threshold[i]:.4f}\n"
    
    cpp_code += "};\n"
    
    with open(filename, 'w') as f:
        f.write(cpp_code)
    
    print(f"C++ code exported to: {filename}")


def main():
    parser = argparse.ArgumentParser(description='Train ML model for trading agent')
    parser.add_argument('--input', type=str, help='Input CSV file with training data')
    parser.add_argument('--output', type=str, default='model.bin', help='Output model file')
    parser.add_argument('--output-cpp', type=str, help='Output C++ header file')
    parser.add_argument('--generate-sample', action='store_true', help='Generate sample data')
    parser.add_argument('--max-depth', type=int, default=5, help='Maximum tree depth')
    args = parser.parse_args()
    
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║       ML Trading Agent Model Training                     ║")
    print("╚═══════════════════════════════════════════════════════════╝")
    
    # Load or generate data
    if args.generate_sample:
        print("\nGenerating sample training data...")
        X, y = generate_sample_data(2000)
    elif args.input:
        print(f"\nLoading data from {args.input}...")
        import pandas as pd
        df = pd.read_csv(args.input)
        # Expect columns: price_ratio, volume_ratio, momentum, spread, volatility, signal
        X = df[['price_ratio', 'volume_ratio', 'momentum', 'spread', 'volatility']].values
        y = df['signal'].values + 1  # Convert -1,0,1 to 0,1,2
    else:
        print("Error: Specify --input or --generate-sample")
        return 1
    
    print(f"Training samples: {len(X)}")
    print(f"Features: price_ratio, volume_ratio, momentum, spread, volatility")
    print(f"Classes: SELL(0), HOLD(1), BUY(2)")
    
    # Train model
    clf = train_decision_tree(X, y, max_depth=args.max_depth)
    
    # Export binary format
    export_tree_to_slonana(clf, args.output)
    
    # Export C++ format if requested
    if args.output_cpp:
        export_tree_to_cpp(clf, args.output_cpp)
    
    print("\n✓ Training complete!")
    return 0


if __name__ == '__main__':
    exit(main())
