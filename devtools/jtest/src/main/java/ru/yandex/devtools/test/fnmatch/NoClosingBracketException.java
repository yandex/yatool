/*
 * Copyright (C) 2008, Florian Koeberle <florianskarten@web.de>
 * Copyright (C) 2008, Florian Köberle <florianskarten@web.de>
 * Copyright (C) 2009, Vasyl' Vavrychuk <vvavrychuk@gmail.com>
 * Copyright (C) 2009, Yann Simon <yann.simon.fr@gmail.com>
 * and other copyright owners as documented in the project's IP log.
 *
 * This program and the accompanying materials are made available
 * under the terms of the Eclipse Distribution License v1.0 which
 * accompanies this distribution, is reproduced below, and is
 * available at http://www.eclipse.org/org/documents/edl-v10.php
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 *
 * - Neither the name of the Eclipse Foundation, Inc. nor the
 *   names of its contributors may be used to endorse or promote
 *   products derived from this software without specific prior
 *   written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package ru.yandex.devtools.fnmatch;

import java.text.MessageFormat;

/**
 * Thrown when a pattern contains a character group which is open to the right
 * side or a character class which is open to the right side.
 */
public class NoClosingBracketException extends InvalidPatternException {
    private static final long serialVersionUID = 1L;

    /**
     * @param indexOfOpeningBracket
     *            the position of the [ character which has no ] character.
     * @param openingBracket
     *            the unclosed bracket.
     * @param closingBracket
     *            the missing closing bracket.
     * @param pattern
     *            the invalid pattern.
     */
    public NoClosingBracketException(final int indexOfOpeningBracket,
            final String openingBracket, final String closingBracket,
            final String pattern) {
        super(createMessage(indexOfOpeningBracket, openingBracket,
                closingBracket, pattern), pattern);
    }

    private static String createMessage(final int indexOfOpeningBracket,
            final String openingBracket, final String closingBracket,
            final String pattern) {
        return MessageFormat.format("No closing {0} found for {1} at index {2}. Pattern: {3}",
                closingBracket, openingBracket,
                Integer.valueOf(indexOfOpeningBracket), pattern);
    }
}
