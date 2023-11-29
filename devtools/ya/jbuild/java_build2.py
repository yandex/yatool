class DeprecatedError(Exception):
    mute = True


def run_build(_):
    msg = (
        '[ya jbuild] is now DEPRECATED.\n'
        'Use [ya make] instead of [ya jbuild].\n'
        'Use [ya maven-import] instead of [ya jbuild maven-import].'
    )

    raise DeprecatedError(msg)
