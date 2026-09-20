# Warning del progetto (non quelli di JUCE, che arrivano da juce_recommended_warning_flags).
# -Wshadow: una variabile che ne oscura un'altra e' quasi sempre un refuso, e nel codice audio
# (`level` del parametro contro `level` del campione) costa ore.
function(xerum_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endfunction()
