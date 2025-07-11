import os
import shutil
import argparse

TEMPLATES = {
    "stm32f4": os.path.join("templates", "stm32f4", "bsp.c"),
}

def list_targets():
    print("Available controllers:")
    for key in TEMPLATES:
        print(f"  - {key}")


def generate(target, out_file):
    template = TEMPLATES.get(target.lower())
    if not template or not os.path.exists(template):
        print(f"Controller '{target}' not supported.")
        return
    shutil.copy(template, out_file)
    print(f"Generated {out_file} from template {template}")


def main():
    parser = argparse.ArgumentParser(description="KDOS BSP generator")
    parser.add_argument("target", nargs="?", help="Target controller identifier")
    parser.add_argument("-o", "--output", default="bsp_mycpu.c", help="Output file")
    args = parser.parse_args()

    if not args.target:
        list_targets()
        return

    generate(args.target, args.output)

if __name__ == "__main__":
    main()
