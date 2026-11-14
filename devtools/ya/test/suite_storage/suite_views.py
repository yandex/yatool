"""Public entry point for pure suite, chunk and test-case read adapters."""

import devtools.ya.test.suite_storage.views as views

Suite = views.Suite
ContainerView = views.ContainerView
ChunkView = views.ChunkView
TestCaseView = views.TestCaseView
SuiteView = views.SuiteView
adapt = views.adapt
