.DEFAULT_GOAL := help


.PHONY: help
help:
	@echo 'Usage: make [setup|develop|pylint|test|benchmark|cover|package|clean]'


.PHONY: setup
setup:
	sudo apt-get install -y python3-dev libmpdec-dev lcov


.PHONY: develop
develop:
	pip install -e .


.PHONY: pylint
pylint:
	pip install pylint
	python3 -m pylint src/abysmal/*.py tests/*.py


.PHONY: test
test: develop
	@echo '---------------------------------------------'
	@echo 'Running tests with $(shell python3 --version)'
	@echo '---------------------------------------------'
	python3 -m unittest -v tests/test_*.py


.PHONY: benchmark
benchmark: clean develop
	@echo '---------------------------------------------'
	@echo 'Running benchmarks with $(shell python3 --version)'
	@echo '---------------------------------------------'
	python3 -m unittest benchmarks/test_*.py


.PHONY: cover
cover: clean
	@echo '---------------------------------------------'
	@echo 'Running coverage with $(shell python3 --version)'
	@echo '---------------------------------------------'
	pip install coverage
	ABYSMAL_DEBUG=1 ABYSMAL_COVER=1 pip install -e .
	mkdir -p build/ccoverage
	mkdir -p build/pycoverage
	python3 -m coverage run --branch --source 'abysmal' -m unittest tests/test_*.py
	python3 -m coverage html -d $(abspath build/pycoverage)
	python3 -m coverage report
	gcov -b -o $$(find . -wholename './build/*/src/abysmal') src/abysmal/*.c
	lcov --capture --directory . --output-file build/ccoverage/coverage.info
	genhtml build/ccoverage/coverage.info --output-directory build/ccoverage


.PHONY: package
package: clean
	python3 setup.py sdist


.PHONY: clean
clean:
	python3 setup.py develop --uninstall
	rm -rf \
		.coverage \
	    *.gcov \
		build \
		dist \
		$$(find . -name '__pycache__') \
		$$(find . -name '*.py[cod]') \
		$$(find . -name '*.so') \
		src/*.egg-info \
		src/*.egg
