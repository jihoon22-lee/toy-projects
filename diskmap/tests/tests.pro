TEMPLATE = subdirs
CONFIG += ordered

# Each test binary defines its own main(), so qmake needs one app project per
# file. CONFIG += testcase in each of them is what generates the `check` target
# that ici's adapter runs.
SUBDIRS = \
    test_format.pro \
    test_fs_node.pro \
    test_fs_source.pro \
    test_scanner.pro \
    test_scanner_safety.pro \
    test_scanner_real_safety.pro \
    test_cleanup.pro \
    test_trash.pro \
    test_duplicates.pro \
    test_snapshot.pro \
    test_treemap.pro \
    test_treemap_widget.pro \
    test_main_window.pro \
    test_storage_workbench.pro \
    test_storage_cli.pro \
    test_view.pro \
    test_node_table_model.pro
