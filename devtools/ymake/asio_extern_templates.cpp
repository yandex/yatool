#include "asio_extern_templates.h"

// Explicit instantiation definitions -- one copy for the entire ymake binary.

template class asio::execution::any_executor<
    asio::execution::context_as_t<asio::execution_context&>,
    asio::execution::blocking_t::never_t,
    asio::execution::prefer_only<asio::execution::blocking_t::possibly_t>,
    asio::execution::prefer_only<asio::execution::outstanding_work_t::tracked_t>,
    asio::execution::prefer_only<asio::execution::outstanding_work_t::untracked_t>,
    asio::execution::prefer_only<asio::execution::relationship_t::fork_t>,
    asio::execution::prefer_only<asio::execution::relationship_t::continuation_t>
>;

template class asio::strand<asio::any_io_executor>;
