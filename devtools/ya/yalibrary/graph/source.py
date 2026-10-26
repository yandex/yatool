class GraphSource(object):
    """A full graph serializer with an eagerly available local lite graph.

    Subclasses implement __call__(output) to serialize the complete graph to a
    binary stream. local_graph supplies metadata without invoking serialization.
    """

    def __init__(self, local_graph):
        self.local_graph = local_graph

    def __call__(self, output):
        raise NotImplementedError
