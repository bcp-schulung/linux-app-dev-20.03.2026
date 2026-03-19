.PHONY: all build clean test install deps help

# Root project variables
PROJECT_NAME=weather-station
TEST_HARNESS_DIR=test-harness
BUILD_DIR=bin
BINARY_NAME=test-harness
CMD_DIR=cmd/harness

# Default target
.DEFAULT_GOAL := help

help: ## Show this help message
	@echo "$(PROJECT_NAME) - build system"
	@echo ""
	@echo "available targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'

# Test Harness targets (Go)
all: deps build ## Download dependencies and build everything

deps: ## Download Go dependencies
	@echo "downloading dependencies..."
	cd $(TEST_HARNESS_DIR) && go mod download
	cd $(TEST_HARNESS_DIR) && go mod tidy

build: deps ## Build the test harness binary
	@echo "building $(BINARY_NAME)..."
	mkdir -p $(BUILD_DIR)
	cd $(TEST_HARNESS_DIR) && go build -ldflags="-s -w" -o $(BUILD_DIR)/$(BINARY_NAME) ./$(CMD_DIR)
	@echo "build complete: $(BUILD_DIR)/$(BINARY_NAME)"

build-debug: ## Build with debug symbols
	@echo "building $(BINARY_NAME) with debug symbols..."
	mkdir -p $(BUILD_DIR)
	cd $(TEST_HARNESS_DIR) && go build -gcflags="all=-N -l" -o $(BUILD_DIR)/$(BINARY_NAME)-debug ./$(CMD_DIR)

run: build ## Build and run the test harness
	$(BUILD_DIR)/$(BINARY_NAME) --help

clean: ## Clean build artifacts
	@echo "cleaning..."
	rm -rf $(BUILD_DIR)
	cd $(TEST_HARNESS_DIR) && go clean -cache

test: ## Run all tests
	@echo "running tests..."
	cd $(TEST_HARNESS_DIR) && go test -v ./...

test-short: ## Run short tests only
	@echo "running short tests..."
	cd $(TEST_HARNESS_DIR) && go test -short ./...

install: build ## Install binary to /usr/local/bin
	@echo "installing..."
	cp $(BUILD_DIR)/$(BINARY_NAME) /usr/local/bin/

# Development helpers
fmt: ## Format Go code
	@echo "formatting code..."
	cd $(TEST_HARNESS_DIR) && go fmt ./...

vet: ## Run go vet
	@echo "running go vet..."
	cd $(TEST_HARNESS_DIR) && go vet ./...

lint: fmt vet ## Run all linters
	@echo "linting complete"

# Data retrieval
retrieve: build ## Download sample weather data
	$(BUILD_DIR)/$(BINARY_NAME) retrieve --limit 1000 --country US

retrieve-de: build ## Download German weather data
	$(BUILD_DIR)/$(BINARY_NAME) retrieve --country de --limit 10000

# Docker targets
docker-build: ## Build Docker image
	cd $(TEST_HARNESS_DIR) && docker build -t weather-station/test-harness:latest .

docker-run: ## Run Docker container
	docker run --rm -v $$(pwd)/services:/services weather-station/test-harness:latest

# CI/CD targets
ci-test: build ## Run CI test suite
	$(BUILD_DIR)/$(BINARY_NAME) ci --fail-threshold 80

validate: build ## Validate services against contracts
	$(BUILD_DIR)/$(BINARY_NAME) validate --service s1_ingestion
	$(BUILD_DIR)/$(BINARY_NAME) validate --service s2_aggregation
	$(BUILD_DIR)/$(BINARY_NAME) validate --service s3_query
	$(BUILD_DIR)/$(BINARY_NAME) validate --service s4_discovery

grade: build ## Grade student submission
	$(BUILD_DIR)/$(BINARY_NAME) grade --detailed

# C services targets (placeholders for future)
build-c: ## Build C services (placeholder)
	@echo "building c services..."
	@echo "todo: implement c build"

test-c: ## Test C services (placeholder)
	@echo "testing c services..."
	@echo "todo: implement c test"

# Full project targets
clean-all: clean ## Clean everything including docs
	@echo "cleaning all artifacts..."
	rm -rf docs/_build

status: ## Show project status
	@echo "$(PROJECT_NAME) status:"
	@echo "  test harness: $(shell test -f $(BUILD_DIR)/$(BINARY_NAME) && echo 'built' || echo 'not built')"
	@echo "  go version: $(shell go version)"
	@echo "  os: $(shell uname -s)"
