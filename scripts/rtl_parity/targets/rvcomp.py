"""RVComp adapter backed by its diagnostics-rich evaluator."""

from evaluate_rvcomp import main


def run(arguments):
    return main(arguments)
