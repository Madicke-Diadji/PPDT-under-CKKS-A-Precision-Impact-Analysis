import argparse
import json
from pathlib import Path


def render_node(node, indent=0):
    prefix = "  " * indent

    if "leaf" in node:
        return [f"{prefix}leaf = {node['leaf']}"]

    internal = node["internal"]
    feature = internal["feature"]
    threshold = internal["threshold"]
    op = internal.get("op", "leq")

    if op == "leq":
        condition = f"f{feature} <= {threshold}"
    elif op == "gt":
        condition = f"f{feature} > {threshold}"
    else:
        condition = f"f{feature} {op} {threshold}"

    lines = [f"{prefix}if {condition}"]
    lines.extend(render_node(internal["left"], indent + 1))
    lines.append(f"{prefix}else")
    lines.extend(render_node(internal["right"], indent + 1))
    return lines


def main():
    parser = argparse.ArgumentParser(
        description="Convert a SortingHat model.json tree into readable plaintext text."
    )
    parser.add_argument("input_json", help="Path to model.json")
    parser.add_argument(
        "-o",
        "--output",
        help="Optional output text file path. If omitted, prints to stdout.",
    )
    args = parser.parse_args()

    input_path = Path(args.input_json)
    with input_path.open("r", encoding="utf-8") as f:
        tree = json.load(f)

    text = "\n".join(render_node(tree)) + "\n"

    if args.output:
        output_path = Path(args.output)
        output_path.write_text(text, encoding="utf-8")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
