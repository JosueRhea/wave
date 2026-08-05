//! A one-line editable text field.
//!
//! The overlay inputs grew up append-only: a string and a backspace. That is
//! enough to type a query and no more — you cannot fix a typo in the middle,
//! select anything, or paste over what is already there. This is the piece that
//! makes a search box behave like a text box: a caret you can move, a selection
//! anchored against it, and the macOS motions (word by ⌥, line by ⌘) every input
//! on the platform is expected to have.
//!
//! `caret` and `anchor` are byte offsets into `text`, always on a char boundary.
//! Equal offsets mean there is no selection, which makes "replace the selection"
//! and "insert at the caret" the same path. Callers that have to push the text
//! somewhere expensive on every change — project search re-runs ripgrep — get a
//! `bool` back from the mutating calls so caret moves cost nothing.

use std::ops::Range;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Motion {
    Left,
    Right,
    WordLeft,
    WordRight,
    Start,
    End,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Delete {
    /// ⌫
    Back,
    /// ⌦
    Forward,
    /// ⌥⌫
    WordBack,
    /// ⌥⌦
    WordForward,
    /// ⌘⌫
    ToStart,
    /// ⌘⌦
    ToEnd,
}

#[derive(Default, Clone, Debug)]
pub struct TextField {
    text: String,
    caret: usize,
    anchor: usize,
}

/// Word characters, for ⌥-motions: a run of these is one word, anything else is
/// a separator. Matches what the editor treats as a word.
fn is_word(c: char) -> bool {
    c.is_alphanumeric() || c == '_'
}

impl TextField {
    /// A field holding `text`, caret at the end — the state focusing a filled
    /// box leaves you in.
    pub fn new(text: &str) -> Self {
        let mut field = Self::default();
        field.set(text);
        field
    }

    pub fn set(&mut self, text: &str) {
        self.text = text.chars().filter(|c| !c.is_control()).collect();
        self.caret = self.text.len();
        self.anchor = self.caret;
    }

    pub fn text(&self) -> &str {
        &self.text
    }

    pub fn is_empty(&self) -> bool {
        self.text.is_empty()
    }

    /// The caret as a column, for drawing: characters to its left, not bytes.
    pub fn caret_col(&self) -> usize {
        self.text[..self.caret].chars().count()
    }

    pub fn selection(&self) -> Option<Range<usize>> {
        (self.caret != self.anchor).then(|| {
            let (a, b) = (self.caret.min(self.anchor), self.caret.max(self.anchor));
            a..b
        })
    }

    pub fn selected_text(&self) -> &str {
        match self.selection() {
            Some(r) => &self.text[r],
            None => "",
        }
    }

    pub fn select_all(&mut self) {
        self.anchor = 0;
        self.caret = self.text.len();
    }

    /// Insert at the caret, replacing the selection. Control characters are
    /// dropped so pasting a multi-line clipboard cannot break the single line.
    /// Returns whether the text changed.
    pub fn insert(&mut self, text: &str) -> bool {
        let insert: String = text.chars().filter(|c| !c.is_control()).collect();
        let had_selection = self.delete_selection();
        if insert.is_empty() {
            return had_selection;
        }
        self.text.insert_str(self.caret, &insert);
        self.caret += insert.len();
        self.anchor = self.caret;
        true
    }

    /// Delete the selection if there is one, else the range `kind` names.
    /// Returns whether the text changed.
    pub fn delete(&mut self, kind: Delete) -> bool {
        if self.delete_selection() {
            return true;
        }
        let range = match kind {
            Delete::Back => self.offset(Motion::Left)..self.caret,
            Delete::Forward => self.caret..self.offset(Motion::Right),
            Delete::WordBack => self.offset(Motion::WordLeft)..self.caret,
            Delete::WordForward => self.caret..self.offset(Motion::WordRight),
            Delete::ToStart => 0..self.caret,
            Delete::ToEnd => self.caret..self.text.len(),
        };
        if range.is_empty() {
            return false;
        }
        self.text.replace_range(range.clone(), "");
        self.caret = range.start;
        self.anchor = self.caret;
        true
    }

    /// Move the caret. `extend` keeps the anchor, growing the selection. Without
    /// it a selection collapses to the edge the caret moved toward rather than
    /// stepping from the caret, which is what every macOS input does.
    pub fn move_caret(&mut self, motion: Motion, extend: bool) {
        if !extend {
            if let Some(sel) = self.selection() {
                match motion {
                    Motion::Left | Motion::WordLeft => {
                        self.caret = sel.start;
                        self.anchor = sel.start;
                        if motion == Motion::Left {
                            return;
                        }
                    }
                    Motion::Right | Motion::WordRight => {
                        self.caret = sel.end;
                        self.anchor = sel.end;
                        if motion == Motion::Right {
                            return;
                        }
                    }
                    Motion::Start | Motion::End => {}
                }
            }
        }
        self.caret = self.offset(motion);
        if !extend {
            self.anchor = self.caret;
        }
    }

    /// Where `motion` lands, as a byte offset on a char boundary.
    fn offset(&self, motion: Motion) -> usize {
        match motion {
            Motion::Start => 0,
            Motion::End => self.text.len(),
            Motion::Left => self.text[..self.caret]
                .chars()
                .next_back()
                .map_or(0, |c| self.caret - c.len_utf8()),
            Motion::Right => self.text[self.caret..]
                .chars()
                .next()
                .map_or(self.caret, |c| self.caret + c.len_utf8()),
            // Skip the separators next to the caret, then the word itself — so
            // ⌥← from "one two |" lands before "two", not in the gap.
            Motion::WordLeft => {
                let mut at = self.caret;
                let head = |at: usize| self.text[..at].chars().next_back();
                while let Some(c) = head(at).filter(|c| !is_word(*c)) {
                    at -= c.len_utf8();
                }
                while let Some(c) = head(at).filter(|c| is_word(*c)) {
                    at -= c.len_utf8();
                }
                at
            }
            Motion::WordRight => {
                let mut at = self.caret;
                let tail = |at: usize| self.text[at..].chars().next();
                while let Some(c) = tail(at).filter(|c| !is_word(*c)) {
                    at += c.len_utf8();
                }
                while let Some(c) = tail(at).filter(|c| is_word(*c)) {
                    at += c.len_utf8();
                }
                at
            }
        }
    }

    /// Drop the selected text, leaving the caret where it was. Returns whether
    /// anything was removed.
    fn delete_selection(&mut self) -> bool {
        let Some(range) = self.selection() else {
            return false;
        };
        self.text.replace_range(range.clone(), "");
        self.caret = range.start;
        self.anchor = range.start;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insert_lands_at_the_caret_not_the_end() {
        let mut f = TextField::new("en camino");
        f.move_caret(Motion::Start, false);
        assert!(f.insert("tu "));
        assert_eq!(f.text(), "tu en camino");
        assert_eq!(f.caret, 3);
    }

    #[test]
    fn set_puts_the_caret_at_the_end_and_drops_control_chars() {
        let f = TextField::new("one\ntwo");
        assert_eq!(f.text(), "onetwo");
        assert_eq!(f.caret, 6);
        assert!(f.selection().is_none());
    }

    #[test]
    fn arrows_walk_the_line_and_stop_at_its_edges() {
        let mut f = TextField::new("ab");
        f.move_caret(Motion::Left, false);
        assert_eq!(f.caret, 1);
        f.move_caret(Motion::Left, false);
        f.move_caret(Motion::Left, false);
        assert_eq!(f.caret, 0);
        f.move_caret(Motion::End, false);
        assert_eq!(f.caret, 2);
        f.move_caret(Motion::Right, false);
        assert_eq!(f.caret, 2);
    }

    #[test]
    fn word_motions_cross_the_gap_with_the_word() {
        let mut f = TextField::new("uno dos tres");
        f.move_caret(Motion::WordLeft, false);
        assert_eq!(&f.text()[f.caret..], "tres");
        f.move_caret(Motion::WordLeft, false);
        assert_eq!(&f.text()[f.caret..], "dos tres");
        f.move_caret(Motion::WordRight, false);
        assert_eq!(&f.text()[f.caret..], " tres");
        f.move_caret(Motion::WordRight, false);
        assert_eq!(f.caret, f.text().len());
    }

    #[test]
    fn backspace_and_delete_work_around_the_caret() {
        let mut f = TextField::new("abcd");
        f.move_caret(Motion::Left, false);
        f.move_caret(Motion::Left, false);
        assert!(f.delete(Delete::Back));
        assert_eq!(f.text(), "acd");
        assert!(f.delete(Delete::Forward));
        assert_eq!(f.text(), "ad");
        assert_eq!(f.caret, 1);
    }

    #[test]
    fn deleting_at_an_edge_changes_nothing() {
        let mut f = TextField::new("a");
        f.move_caret(Motion::Start, false);
        assert!(!f.delete(Delete::Back));
        f.move_caret(Motion::End, false);
        assert!(!f.delete(Delete::Forward));
        assert_eq!(f.text(), "a");
    }

    #[test]
    fn word_and_line_deletes() {
        let mut f = TextField::new("uno dos tres");
        assert!(f.delete(Delete::WordBack));
        assert_eq!(f.text(), "uno dos ");
        assert!(f.delete(Delete::ToStart));
        assert_eq!(f.text(), "");

        let mut f = TextField::new("uno dos");
        f.move_caret(Motion::Start, false);
        assert!(f.delete(Delete::WordForward));
        assert_eq!(f.text(), " dos");
        f.move_caret(Motion::Start, false);
        assert!(f.delete(Delete::ToEnd));
        assert_eq!(f.text(), "");
    }

    #[test]
    fn shift_extends_a_selection_and_typing_replaces_it() {
        let mut f = TextField::new("uno dos");
        f.move_caret(Motion::WordLeft, true);
        assert_eq!(f.selected_text(), "dos");
        assert!(f.insert("tres"));
        assert_eq!(f.text(), "uno tres");
        assert!(f.selection().is_none());
    }

    #[test]
    fn select_all_then_backspace_empties_the_field() {
        let mut f = TextField::new("en camino");
        f.select_all();
        assert_eq!(f.selected_text(), "en camino");
        assert!(f.delete(Delete::Back));
        assert!(f.is_empty());
        assert_eq!(f.caret, 0);
    }

    #[test]
    fn a_plain_arrow_collapses_a_selection_to_the_side_it_moved() {
        let mut f = TextField::new("uno dos");
        f.select_all();
        f.move_caret(Motion::Left, false);
        assert_eq!(f.caret, 0);
        f.select_all();
        f.move_caret(Motion::Right, false);
        assert_eq!(f.caret, f.text().len());
        assert!(f.selection().is_none());
    }

    #[test]
    fn multi_byte_text_moves_and_deletes_by_character() {
        let mut f = TextField::new("está");
        assert_eq!(f.caret, 5);
        assert_eq!(f.caret_col(), 4);
        f.move_caret(Motion::Left, false);
        assert_eq!(f.caret, 3);
        assert!(f.delete(Delete::Back));
        assert_eq!(f.text(), "esá");
    }

    #[test]
    fn a_caret_move_reports_no_text_change() {
        let mut f = TextField::new("abc");
        f.move_caret(Motion::Left, false);
        assert!(!f.insert(""));
    }
}
