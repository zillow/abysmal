from collections import namedtuple

CoverageReport = namedtuple('CoverageReport', ['partially_covered_line_numbers', 'uncovered_line_numbers'])

def get_uncovered_lines(source_map, coverage_tuples):
    """
    Combines an Abysmal program source map and an iterable
    of coverage tuples produced by zero or more calls to
    machine.run_with_coverage() to produce a coverage
    report.

    Returns a CoverageReport.
    """
    line_to_coverage_type = {} # 0 = uncovered, 1 = partial, 2 = covered
    # Logical-OR the individual coverage tuples together.
    for idx_instruction, hit in enumerate(any(covered) for covered in zip((False,) * len(source_map), *coverage_tuples)):
        line = source_map[idx_instruction]
        if line is not None: # ignore instructions that have no source line (typically Xx opcodes)
            if line not in line_to_coverage_type:
                line_to_coverage_type[line] = 2 if hit else 0
            elif (not hit and line_to_coverage_type[line] == 2) or (hit and line_to_coverage_type[line] == 0):
                line_to_coverage_type[line] = 1
    partially_covered_lines = []
    uncovered_lines = []
    for line, coverage_type in line_to_coverage_type.items():
        if coverage_type == 2: # pragma: no branch
            continue # ignore totally-covered lines
        lines = uncovered_lines if coverage_type == 0 else partially_covered_lines
        if isinstance(line, tuple):
            lines.extend(list(range(line[0], line[1] + 1))) # multi-line statement
        else:
            lines.append(line) # single-line statement
    return CoverageReport(
        sorted(partially_covered_lines),
        sorted(uncovered_lines)
    )
