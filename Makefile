.DEFAULT_GOAL := help


.PHONY: help
help:
	@echo 'Usage: make [setup|develop|test|pylint|cover|clean]'


.PHONY: setup
setup:
	sudo apt-get install python3-dev libmpdec-dev


.PHONY: develop
develop:
	pip install -e .


.PHONY: test
test: develop
	@echo '---------------------------------------------'
	@echo 'Running tests with $(shell python3 --version)'
	@echo '---------------------------------------------'
	python3 -m unittest -v tests/test_*.py


.PHONY: pylint
pylint: develop
	pip install pylint
	python3 -m pylint src/abysmal/*.py tests/*.py


.PHONY: cover
cover: develop
	pip install coverage
	python3 -E -m coverage run --branch --source 'abysmal' -m unittest tests/test_*.py
	python3 -E -m coverage html -d $(abspath build/coverage)
	python3 -E -m coverage report


.PHONY: commit
commit: pylint test cover


.PHONY: clean
clean:
	rm -rf \
		.coverage \
		build \
		dist \
		$$(find . -name '__pycache__') \
		$$(find . -name '*.py[cod]') \
		$$(find . -name '*.so') \
		*.egg-info \
		*.egg
