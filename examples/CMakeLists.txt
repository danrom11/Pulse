add_subdirectory(minimal_bus)
add_subdirectory(search_with_cancel)
add_subdirectory(search_async)
add_subdirectory(operators_demo)
add_subdirectory(distinct_timeout_demo)
add_subdirectory(search_with_pool)
add_subdirectory(share_demo)
add_subdirectory(search_box_service)
add_subdirectory(combine_latest_demo)
add_subdirectory(zip_interval_demo)
add_subdirectory(subject_demo)
add_subdirectory(publish_refcount_demo)
add_subdirectory(publish_refcount_grace_demo)




if (PULSE_WITH_QT)
  add_subdirectory(qt_search_demo)
endif()
