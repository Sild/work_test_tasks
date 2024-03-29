#include "Matcher.hpp"

#include <string.h>
#include <assert.h>

namespace {
    // modify the https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html
    bool matchstar(char *aFilter, const char *aLine);

    /* matchhere: search for regexp at beginning of text */
    bool matchhere(char *aFilter, const char *aLine)
    {
        if (aFilter[0] == '*')
            return matchstar(aFilter+1, aLine);
        if (aFilter[0] == '\0')
            return *aLine == '\0';
        if (*aLine != '\0' && (aFilter[0] == '?' || aFilter[0] == *aLine))
            return matchhere(aFilter+1, aLine+1);
        return false;
    }

    /* matchstar: search for c*regexp at beginning of text */
    bool matchstar(char *aFilter, const char *aLine)
    {
        if (aFilter[0] == '\0')
            return true;
        do {    /* a * matches zero or more instances */
            if (matchhere(aFilter, aLine))
                return true;
        } while (*aLine++ != '\0');
        return false;
    }

    bool match(char* aFilter, const char* aLine) {
        assert(aFilter != NULL);
        // bad asymtotic M * N for start case =(
        return matchhere(aFilter, aLine);
    }
}


Matcher::Matcher(const char* aFilter)
: m_FilterEmpty(true)
, m_Filter(NULL)
{
    SetFilter(aFilter);
}

Matcher::~Matcher() {
    delete[] m_Filter;
}

bool Matcher::SetFilter(const char* aFilter) {
    if (aFilter == NULL || aFilter == '\0') {
        delete[] m_Filter;
        m_Filter = NULL;
        m_FilterEmpty = true;
        return true;
    }

    // need some validation of aFilter?
    
    // may be optimized to reuse old memory, then should handle aFilter == NULL withour delete[]
    {
        delete[] m_Filter;
        m_Filter = new char[strlen(aFilter)+ 1];
        strcpy(m_Filter, aFilter);
        m_FilterEmpty = false;
    }
    return true;
}

bool Matcher::Complient(const char* aLine) {
    if (m_FilterEmpty)
        return true;

    return match(m_Filter, aLine);
}