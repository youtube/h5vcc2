import argparse
import re
import sys


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('files', nargs='*')
  args = parser.parse_args()

  errors = []
  # This regex looks for String.format(...) with a "%d" in the format string,
  # where the rest of the line does not contain "Locale.US". This is a
  # heuristic for single-line calls.
  pattern = re.compile(
      r'\bString\.format\((?!.*Locale\.US)(?=.*".*?\%d.*").*?\)')
      
  for file_path in args.files:
    try:
      with open(file_path, 'r', encoding='utf-8') as f:
        for line_num, line in enumerate(f, 1):
          if pattern.search(line):
            errors.append(f'{file_path}:{line_num}: String.format call '
                          'should include a java.util.Locale argument.')
    except Exception as e:
      print(f"Error processing file {file_path}: {e}", file=sys.stderr)

  if errors:
    for error in errors:
      print(error, file=sys.stderr)
    sys.exit(1)


if __name__ == '__main__':
  main()
