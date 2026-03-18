#pragma once

// Extern template declarations for the most expensive asio template
// specializations. Including this header prevents redundant instantiation
// in each TU; the single instantiation lives in asio_extern_templates.cpp.
//
// Data source: -ftime-trace profiling (683,719ms total asio cost).
// Only non-lambda specializations are extern-able.

#include <asio/any_io_executor.hpp>
#include <asio/strand.hpp>

// #1: any_executor<...> -- 81s aggregate across all TUs
extern template class asio::execution::any_executor<
    asio::execution::context_as_t<asio::execution_context&>,
    asio::execution::blocking_t::never_t,
    asio::execution::prefer_only<asio::execution::blocking_t::possibly_t>,
    asio::execution::prefer_only<asio::execution::outstanding_work_t::tracked_t>,
    asio::execution::prefer_only<asio::execution::outstanding_work_t::untracked_t>,
    asio::execution::prefer_only<asio::execution::relationship_t::fork_t>,
    asio::execution::prefer_only<asio::execution::relationship_t::continuation_t>
>;

// #2: strand<any_io_executor> -- used as TConfigurationExecutor
extern template class asio::strand<asio::any_io_executor>;
