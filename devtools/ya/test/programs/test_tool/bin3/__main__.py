if __name__ == '__main__':
    from devtools.ya.test.programs.test_tool import main_entry_point

    executor = main_entry_point.get_executor()
    exit(executor())
