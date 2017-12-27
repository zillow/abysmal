.DEFAULT_GOAL := help


.PHONY: help
help:
	@echo 'Usage: make [setup|develop|pylint|test|cover|package|clean]'


.PHONY: setup
setup:
	sudo apt-get install python3-dev libmpdec-dev


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


.PHONY: cover
cover: develop
	pip install coverage
	python3 -m coverage run --branch --source 'abysmal' -m unittest tests/test_*.py
	python3 -m coverage html -d $(abspath build/coverage)
	python3 -m coverage report


.PHONY: commit
commit: pylint test cover


.PHONY: package
package: clean
	python3 setup.py sdist


.PHONY: clean
clean:
	python3 setup.py develop --uninstall
	rm -rf \
		.coverage \
		build \
		dist \
		$$(find . -name '__pycache__') \
		$$(find . -name '*.py[cod]') \
		$$(find . -name '*.so') \
		src/*.egg-info \
		src/*.egg
